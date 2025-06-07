#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "commands/AuthCommands.hpp"
#include "commands/ChannelCommands.hpp"
#include "commands/MessageCommands.hpp"
#include <stdexcept>
#include <cstring>    // pour strerror
#include <cerrno>     // pour errno
#include <sstream>    // CORRIGÉ: ajouté pour ostringstream

// Fonction utilitaire C++98 pour convertir int en string
std::string intToString(int value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

// Constructeur : initialise le serveur avec port et password
Server::Server(int port, const std::string& password) 
    : _port(port), _password(password), _server_fd(-1), _running(false) {
    
    std::cout << "Initializing IRC Server..." << std::endl;
    
    // Initialiser la structure d'adresse à zéro
    std::memset(&_server_addr, 0, sizeof(_server_addr));
    
    try {
        _setupSocket();     // Créer et configurer le socket
        _bindAndListen();   // Bind sur le port et mettre en écoute
        
        std::cout << "Server initialized successfully on port " << _port << std::endl;
        
    } catch (const std::exception& e) {
        // En cas d'erreur, nettoyer et relancer l'exception
        if (_server_fd != -1) {
            close(_server_fd);
            _server_fd = -1;
        }
        throw; // Relancer l'exception
    }
}

// Destructeur : nettoyer toutes les ressources
Server::~Server() {
    std::cout << "Shutting down IRC Server..." << std::endl;
    
    // Déconnecter tous les clients
    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        delete it->second;  // Supprimer l'objet Client
        close(it->first);   // Fermer le socket
    }
    _clients.clear();
    
    // Fermer le socket serveur
    if (_server_fd != -1) {
        close(_server_fd);
    }
    
    std::cout << "Server shutdown complete" << std::endl;
}

// Créer et configurer le socket serveur
void Server::_setupSocket() {
    // Créer un socket TCP IPv4
    _server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_server_fd < 0) {
        throw std::runtime_error("Failed to create socket: " + std::string(strerror(errno)));
    }
    
    // Option SO_REUSEADDR : permet de réutiliser l'adresse immédiatement
    // Évite l'erreur "Address already in use" après redémarrage
    int opt = 1;
    if (setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("Failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
    }
    
    // Configurer le socket en mode non-bloquant
    // F_SETFL = Set File status fLags, O_NONBLOCK = non-bloquant
    if (fcntl(_server_fd, F_SETFL, O_NONBLOCK) < 0) {
        throw std::runtime_error("Failed to set non-blocking mode: " + std::string(strerror(errno)));
    }
    
    std::cout << "Socket created and configured" << std::endl;
}

// Associer le socket à une adresse et mettre en écoute
void Server::_bindAndListen() {
    // Configurer l'adresse du serveur
    _server_addr.sin_family = AF_INET;         // IPv4
    _server_addr.sin_addr.s_addr = INADDR_ANY; // Écouter sur toutes les interfaces
    _server_addr.sin_port = htons(_port);      // Port en format réseau (big-endian)
    
    // Associer le socket à l'adresse
    if (bind(_server_fd, (struct sockaddr*)&_server_addr, sizeof(_server_addr)) < 0) {
        // CORRIGÉ: std::to_string remplacé par intToString
        throw std::runtime_error("Failed to bind to port " + intToString(_port) + ": " + std::string(strerror(errno)));
    }
    
    // Mettre le socket en mode écoute
    // 10 = taille de la queue des connexions en attente
    if (listen(_server_fd, 10) < 0) {
        throw std::runtime_error("Failed to listen on socket: " + std::string(strerror(errno)));
    }
    
    std::cout << "Server bound and listening on port " << _port << std::endl;
}

// Démarrer le serveur et entrer dans la boucle principale
void Server::start() {
    std::cout << "Starting IRC Server main loop..." << std::endl;
    
    _running = true;
    
    // Ajouter le socket serveur à la liste de surveillance poll()
    _addToPoll(_server_fd, POLLIN);
    
    try {
        _runEventLoop(); // Boucle principale
    } catch (const std::exception& e) {
        std::cerr << "Error in main loop: " << e.what() << std::endl;
        stop();
        throw;
    }
}

// Arrêter le serveur
void Server::stop() {
    std::cout << "Stopping server..." << std::endl;
    _running = false;
}

// Boucle principale du serveur avec poll()
void Server::_runEventLoop() {
    while (_running) {
        // poll() surveille tous les file descriptors et attend des événements
        // -1 = timeout infini (attendre indéfiniment)
        int poll_result = poll(&_poll_fds[0], _poll_fds.size(), -1);
        
        if (poll_result < 0) {
            if (errno == EINTR) {
                // Interrompu par un signal (normal), continuer
                continue;
            }
            throw std::runtime_error("poll() failed: " + std::string(strerror(errno)));
        }
        
        // Parcourir tous les file descriptors pour voir lesquels ont des événements
        for (size_t i = 0; i < _poll_fds.size(); ++i) {
            
            // Vérifier s'il y a des événements sur ce FD
            if (_poll_fds[i].revents == 0) {
                continue; // Pas d'événements, passer au suivant
            }
            
            // Le serveur (toujours à l'index 0) a une nouvelle connexion
            if (_poll_fds[i].fd == _server_fd && (_poll_fds[i].revents & POLLIN)) {
                _acceptNewClient();
            }
            // Un client existant a envoyé des données
            else if (_poll_fds[i].revents & POLLIN) {
                _handleClientData(_poll_fds[i].fd);
            }
            // Erreur sur un socket (connexion fermée, etc.)
            else if (_poll_fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                std::cout << "Client disconnected (socket error)" << std::endl;
                _disconnectClient(_poll_fds[i].fd);
                --i; // Ajuster l'index car on a supprimé un élément
            }
        }
    }
}

// Accepter une nouvelle connexion client
void Server::_acceptNewClient() {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Accepter la connexion
    int client_fd = accept(_server_fd, (struct sockaddr*)&client_addr, &client_len);
    
    if (client_fd < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            std::cerr << "Failed to accept client: " << strerror(errno) << std::endl;
        }
        return; // Pas d'erreur fatale, continuer
    }
    
    // Configurer le socket client en mode non-bloquant
    if (fcntl(client_fd, F_SETFL, O_NONBLOCK) < 0) {
        std::cerr << "Failed to set client socket non-blocking: " << strerror(errno) << std::endl;
        close(client_fd);
        return;
    }
    
    // Récupérer l'adresse IP du client
    std::string client_ip = inet_ntoa(client_addr.sin_addr);
    
    // Créer un objet Client pour ce nouveau client
    Client* new_client = new Client(client_fd, client_ip);
    
    // Ajouter le client à nos structures de données
    _clients[client_fd] = new_client;
    _addToPoll(client_fd, POLLIN);
    
    std::cout << "New client connected from " << client_ip 
              << " (fd: " << client_fd << ")" << std::endl;
}

// Traiter les données reçues d'un client
void Server::_handleClientData(int client_fd) {
    // Vérifier que le client existe
    std::map<int, Client*>::iterator it = _clients.find(client_fd);
    if (it == _clients.end()) {
        std::cerr << "Received data from unknown client fd: " << client_fd << std::endl;
        return;
    }
    
    Client* client = it->second;
    char buffer[1024];
    
    // Recevoir les données
    ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            std::cout << "Client " << client_fd << " disconnected" << std::endl;
        } else {
            std::cerr << "Error receiving from client " << client_fd 
                      << ": " << strerror(errno) << std::endl;
        }
        _disconnectClient(client_fd);
        return;
    }
    
    // Terminer la chaîne et l'ajouter au buffer du client
    buffer[bytes_received] = '\0';
    
    // DEBUG: Afficher les bytes bruts reçus
    std::cout << "Raw bytes received (" << bytes_received << "): ";
    for (ssize_t i = 0; i < bytes_received; ++i) {
        if (buffer[i] >= 32 && buffer[i] <= 126) {
            std::cout << buffer[i];
        } else {
            std::cout << "[" << (int)buffer[i] << "]";
        }
    }
    std::cout << std::endl;
    
    client->appendToReceiveBuffer(std::string(buffer));
    
    // DEBUG: Afficher le buffer complet
    std::cout << "Complete buffer now: '" << client->getSendBuffer() << "'" << std::endl;
    
    // Traiter tous les messages complets disponibles
    while (client->hasCompleteMessage()) {
        std::string message = client->extractMessage();
        std::cout << "Received from " << client_fd << ": " << message << std::endl;
        
        // Parser et traiter la commande IRC
        _parseCommand(client, message);
    }
}

// Déconnecter un client
void Server::_disconnectClient(int client_fd) {
    // Trouver le client
    std::map<int, Client*>::iterator it = _clients.find(client_fd);
    if (it != _clients.end()) {
        delete it->second;      // Supprimer l'objet Client
        _clients.erase(it);     // Supprimer de la map
    }
    
    // Supprimer de poll() et fermer le socket
    _removeFromPoll(client_fd);
    close(client_fd);
    
    std::cout << "Client " << client_fd << " fully disconnected" << std::endl;
}

// Ajouter un file descriptor à la surveillance poll()
void Server::_addToPoll(int fd, short events) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    
    _poll_fds.push_back(pfd);
}

// Supprimer un file descriptor de la surveillance poll()
void Server::_removeFromPoll(int fd) {
    for (std::vector<struct pollfd>::iterator it = _poll_fds.begin(); it != _poll_fds.end(); ++it) {
        if (it->fd == fd) {
            _poll_fds.erase(it);
            break;
        }
    }
}

// Parser les commandes IRC reçues
void Server::_parseCommand(Client* client, const std::string& message) {
    if (message.empty()) {
        return;
    }
    
    std::cout << "Parsing command: '" << message << "'" << std::endl;
    
    // Séparer la commande des arguments
    size_t space_pos = message.find(' ');
    std::string command;
    std::string args;
    
    if (space_pos != std::string::npos) {
        command = message.substr(0, space_pos);
        args = message.substr(space_pos + 1);
    } else {
        command = message;
        args = "";
    }
    
    // Convertir en majuscules pour la comparaison
    for (size_t i = 0; i < command.length(); ++i) {
        if (command[i] >= 'a' && command[i] <= 'z') {
            command[i] = command[i] - 'a' + 'A';
        }
    }
    
    std::cout << "Command: '" << command << "', Args: '" << args << "'" << std::endl;
    
    // Traiter les différentes commandes
    if (command == "PASS") {
        AuthCommands::handlePass(this, client, args);
    } else if (command == "NICK") {
        AuthCommands::handleNick(this, client, args);
    } else if (command == "USER") {
        AuthCommands::handleUser(this, client, args);
    } else if (command == "JOIN") {
        ChannelCommands::handleJoin(this, client, args);
    } else if (command == "PRIVMSG") {
        MessageCommands::handlePrivmsg(this, client, args);
    } else if (command == "KICK") {
        ChannelCommands::handleKick(this, client, args);
    } else if (command == "INVITE") {
        ChannelCommands::handleInvite(this, client, args);
    } else if (command == "TOPIC") {
        ChannelCommands::handleTopic(this, client, args);
    } else if (command == "MODE") {
        ChannelCommands::handleMode(this, client, args);
    } else {
        std::cout << "Unknown command: " << command << std::endl;
        sendResponse(client, "421 * " + command + " :Unknown command\r\n");
    }
}



// Envoyer une réponse à un client
void Server::sendResponse(Client* client, const std::string& response) {
    std::cout << "Sending to client " << client->getFd() << ": " << response;
    
    // Ajouter la réponse au buffer d'envoi du client
    client->appendToSendBuffer(response);
    
    // Essayer d'envoyer immédiatement
    const std::string& send_buffer = client->getSendBuffer();
    if (!send_buffer.empty()) {
        ssize_t bytes_sent = send(client->getFd(), send_buffer.c_str(), send_buffer.length(), 0);
        
        if (bytes_sent > 0) {
            client->clearSendBuffer(bytes_sent);
            std::cout << "Sent " << bytes_sent << " bytes to client " << client->getFd() << std::endl;
        } else if (bytes_sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
            std::cerr << "Error sending to client " << client->getFd() 
                      << ": " << strerror(errno) << std::endl;
        }
    }
}



// Gérer la commande USER (définir username et realname)
void Server::_handleUserCommand(Client* client, const std::string& args) {
    std::cout << "Handling USER command for client " << client->getFd() << std::endl;
    
    if (args.empty()) {
        _sendResponse(client, "461 * USER :Not enough parameters\r\n");
        return;
    }
    
    // Format USER: username hostname servername :realname
    // Exemple: USER john localhost localhost :John Doe
    
    // Parser les arguments (on simplifie pour l'instant)
    size_t colon_pos = args.find(" :");
    std::string username_part;
    std::string realname;
    
    if (colon_pos != std::string::npos) {
        username_part = args.substr(0, colon_pos);
        realname = args.substr(colon_pos + 2); // +2 pour passer " :"
    } else {
        username_part = args;
        realname = "Unknown";
    }
    
    // Extraire juste le premier mot comme username
    size_t space_pos = username_part.find(' ');
    std::string username = (space_pos != std::string::npos) ? 
                          username_part.substr(0, space_pos) : username_part;
    
    client->setUsername(username);
    client->setRealname(realname);
    
    std::cout << "Client " << client->getFd() 
              << " user info set - Username: " << username 
              << ", Realname: " << realname << std::endl;
    
    // Si le client est maintenant complètement enregistré, envoyer les messages de bienvenue
    if (client->isAuthenticated()) {
        _sendWelcomeMessages(client);
    }
}

// Envoyer les messages de bienvenue IRC
void Server::_sendWelcomeMessages(Client* client) {
    std::cout << "Sending welcome messages to " << client->getNickname() << std::endl;
    
    std::string nick = client->getNickname();
    
    // 001 RPL_WELCOME
    _sendResponse(client, "001 " + nick + " :Welcome to the Internet Relay Network " + nick + "\r\n");
    
    // 002 RPL_YOURHOST  
    _sendResponse(client, "002 " + nick + " :Your host is localhost, running version 1.0\r\n");
    
    // 003 RPL_CREATED
    _sendResponse(client, "003 " + nick + " :This server was created today\r\n");
    
    // 004 RPL_MYINFO
    _sendResponse(client, "004 " + nick + " localhost 1.0 o o\r\n");
}

// Gérer la commande JOIN (rejoindre un channel)
void Server::_handleJoinCommand(Client* client, const std::string& args) {
    std::cout << "Handling JOIN command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        _sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        _sendResponse(client, "461 " + client->getNickname() + " JOIN :Not enough parameters\r\n");
        return;
    }
    
    // Parser: JOIN <channel> [<key>]
    std::string channel_name;
    std::string key;
    
    size_t space_pos = args.find(' ');
    if (space_pos != std::string::npos) {
        channel_name = args.substr(0, space_pos);
        key = args.substr(space_pos + 1);
    } else {
        channel_name = args;
    }
    
    // Vérifier que ça commence par #
    if (channel_name[0] != '#') {
        channel_name = "#" + channel_name;
    }
    
    std::cout << "Client " << client->getNickname() << " joining channel " << channel_name << std::endl;
    
    // Obtenir ou créer le channel
    Channel* channel = _getOrCreateChannel(channel_name);
    
    // Vérifier les modes du channel
    
    // Mode +k : Vérifier le mot de passe
    if (!channel->getKey().empty() && channel->getKey() != key) {
        _sendResponse(client, "475 " + client->getNickname() + " " + channel_name + " :Cannot join channel (+k)\r\n");
        return;
    }
    
    // Mode +i : Channel invitation seulement (pour l'instant on laisse passer, sera géré avec INVITE)
    if (channel->isInviteOnly()) {
        // TODO: Vérifier si le client a été invité
        _sendResponse(client, "473 " + client->getNickname() + " " + channel_name + " :Cannot join channel (+i)\r\n");
        return;
    }
    
    // Mode +l : Vérifier la limite d'utilisateurs
    if (channel->getUserLimit() > 0 && channel->getMembers().size() >= static_cast<size_t>(channel->getUserLimit())) {
        _sendResponse(client, "471 " + client->getNickname() + " " + channel_name + " :Cannot join channel (+l)\r\n");
        return;
    }
    
    // Le premier membre devient automatiquement opérateur
    bool is_operator = channel->getMembers().empty();
    
    // Ajouter le client au channel
    channel->addMember(client, is_operator);
    
    // Envoyer confirmation de JOIN à l'utilisateur
    std::string join_msg = ":" + client->getNickname() + " JOIN " + channel_name + "\r\n";
    _sendResponse(client, join_msg);
    
    // Broadcaster le JOIN aux autres membres
    channel->broadcastMessage(join_msg, client);
}

// Gérer la commande PRIVMSG (envoyer un message)
void Server::_handlePrivmsgCommand(Client* client, const std::string& args) {
    std::cout << "Handling PRIVMSG command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        _sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        _sendResponse(client, "461 " + client->getNickname() + " PRIVMSG :Not enough parameters\r\n");
        return;
    }
    
    // Parser: PRIVMSG <target> :<message>
    size_t space_pos = args.find(' ');
    if (space_pos == std::string::npos) {
        _sendResponse(client, "461 " + client->getNickname() + " PRIVMSG :Not enough parameters\r\n");
        return;
    }
    
    std::string target = args.substr(0, space_pos);
    std::string message_part = args.substr(space_pos + 1);
    
    if (message_part.empty() || message_part[0] != ':') {
        _sendResponse(client, "461 " + client->getNickname() + " PRIVMSG :Not enough parameters\r\n");
        return;
    }
    
    std::string message = message_part.substr(1); // Enlever le ':'
    
    std::cout << "PRIVMSG from " << client->getNickname() << " to " << target << ": " << message << std::endl;
    
    // Construire le message IRC à envoyer
    std::string irc_message = ":" + client->getNickname() + " PRIVMSG " + target + " :" + message + "\r\n";
    
    // Vérifier si c'est un channel (commence par #)
    if (target[0] == '#') {
        // Message vers un channel
        std::map<std::string, Channel*>::iterator it = _channels.find(target);
        if (it != _channels.end()) {
            Channel* channel = it->second;
            if (channel->isMember(client)) {
                // Broadcaster le message aux autres membres du channel
                channel->broadcastMessage(irc_message, client);
            } else {
                _sendResponse(client, "404 " + client->getNickname() + " " + target + " :Cannot send to channel\r\n");
            }
        } else {
            _sendResponse(client, "403 " + client->getNickname() + " " + target + " :No such channel\r\n");
        }
    } else {
        // Message privé vers un utilisateur
        Client* target_client = _findClientByNickname(target);
        if (target_client != NULL) {
            // Envoyer le message au client cible
            target_client->appendToSendBuffer(irc_message);
            
            // Essayer d'envoyer immédiatement
            const std::string& send_buffer = target_client->getSendBuffer();
            if (!send_buffer.empty()) {
                ssize_t bytes_sent = send(target_client->getFd(), send_buffer.c_str(), send_buffer.length(), 0);
                
                if (bytes_sent > 0) {
                    target_client->clearSendBuffer(bytes_sent);
                    std::cout << "Private message sent from " << client->getNickname() 
                              << " to " << target << ": " << message << std::endl;
                } else if (bytes_sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
                    std::cerr << "Error sending private message to " << target << ": " 
                              << strerror(errno) << std::endl;
                }
            }
        } else {
            _sendResponse(client, "401 " + client->getNickname() + " " + target + " :No such nick/channel\r\n");
        }
    }
}

// Obtenir ou créer un channel
Channel* Server::getOrCreateChannel(const std::string& name) {
    std::map<std::string, Channel*>::iterator it = _channels.find(name);
    
    if (it != _channels.end()) {
        return it->second; // Channel existe déjà
    }
    
    // Créer un nouveau channel
    Channel* new_channel = new Channel(name);
    _channels[name] = new_channel;
    
    std::cout << "Created new channel: " << name << std::endl;
    return new_channel;
}

// Supprimer un channel vide
void Server::removeEmptyChannel(const std::string& name) {
    std::map<std::string, Channel*>::iterator it = _channels.find(name);
    
    if (it != _channels.end() && it->second->isEmpty()) {
        delete it->second;
        _channels.erase(it);
        std::cout << "Removed empty channel: " << name << std::endl;
    }
}

// Trouver un client par son nickname
Client* Server::findClientByNickname(const std::string& nickname) {
    // Parcourir tous les clients connectés
    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        Client* client = it->second;
        if (client->getNickname() == nickname) {
            return client;
        }
    }
    return NULL; // Client non trouvé
}

// Gérer la commande KICK (éjecter un utilisateur d'un channel)
void Server::_handleKickCommand(Client* client, const std::string& args) {
    std::cout << "Handling KICK command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        _sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        _sendResponse(client, "461 " + client->getNickname() + " KICK :Not enough parameters\r\n");
        return;
    }
    
    // Parser: KICK <channel> <user> [:<reason>]
    // Exemple: KICK #general alice :Spamming
    
    std::string remaining = args;
    std::string channel_name, target_nick, reason;
    
    // Extraire le nom du channel
    size_t space_pos = remaining.find(' ');
    if (space_pos == std::string::npos) {
        _sendResponse(client, "461 " + client->getNickname() + " KICK :Not enough parameters\r\n");
        return;
    }
    
    channel_name = remaining.substr(0, space_pos);
    remaining = remaining.substr(space_pos + 1);
    
    // Extraire le nickname cible
    space_pos = remaining.find(' ');
    if (space_pos == std::string::npos) {
        // Pas de raison fournie
        target_nick = remaining;
        reason = client->getNickname(); // Raison par défaut
    } else {
        target_nick = remaining.substr(0, space_pos);
        std::string reason_part = remaining.substr(space_pos + 1);
        
        // Vérifier si la raison commence par ':'
        if (!reason_part.empty() && reason_part[0] == ':') {
            reason = reason_part.substr(1);
        } else {
            reason = reason_part;
        }
    }
    
    std::cout << "KICK: " << client->getNickname() << " wants to kick " 
              << target_nick << " from " << channel_name 
              << " (reason: " << reason << ")" << std::endl;
    
    // Vérifier que le channel existe
    std::map<std::string, Channel*>::iterator channel_it = _channels.find(channel_name);
    if (channel_it == _channels.end()) {
        _sendResponse(client, "403 " + client->getNickname() + " " + channel_name + " :No such channel\r\n");
        return;
    }
    
    Channel* channel = channel_it->second;
    
    // Vérifier que l'utilisateur qui kick est dans le channel
    if (!channel->isMember(client)) {
        _sendResponse(client, "442 " + client->getNickname() + " " + channel_name + " :You're not on that channel\r\n");
        return;
    }
    
    // Vérifier que l'utilisateur qui kick est opérateur
    if (!channel->isOperator(client)) {
        _sendResponse(client, "482 " + client->getNickname() + " " + channel_name + " :You're not channel operator\r\n");
        return;
    }
    
    // Trouver l'utilisateur cible
    Client* target_client = _findClientByNickname(target_nick);
    if (target_client == NULL) {
        _sendResponse(client, "401 " + client->getNickname() + " " + target_nick + " :No such nick/channel\r\n");
        return;
    }
    
    // Vérifier que l'utilisateur cible est dans le channel
    if (!channel->isMember(target_client)) {
        _sendResponse(client, "441 " + client->getNickname() + " " + target_nick + " " + channel_name + " :They aren't on that channel\r\n");
        return;
    }
    
    // Construire le message KICK
    std::string kick_message = ":" + client->getNickname() + " KICK " + channel_name + " " + target_nick + " :" + reason + "\r\n";
    
    // Envoyer le KICK à tous les membres du channel (y compris l'utilisateur qui kick et celui qui est kické)
    channel->broadcastMessage(kick_message, NULL); // NULL = envoyer à tous
    
    // Retirer l'utilisateur du channel
    channel->removeMember(target_client);
    
    std::cout << "Successfully kicked " << target_nick << " from " << channel_name << std::endl;
    
    // Supprimer le channel s'il est vide
    _removeEmptyChannel(channel_name);
}

// Gérer la commande INVITE (inviter un client au channel)
void Server::_handleInviteCommand(Client* client, const std::string& args) {
    std::cout << "Handling INVITE command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        _sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        _sendResponse(client, "461 " + client->getNickname() + " INVITE :Not enough parameters\r\n");
        return;
    }
    
    // Parser: INVITE <nickname> <channel>
    size_t space_pos = args.find(' ');
    if (space_pos == std::string::npos) {
        _sendResponse(client, "461 " + client->getNickname() + " INVITE :Not enough parameters\r\n");
        return;
    }
    
    std::string target_nick = args.substr(0, space_pos);
    std::string channel_name = args.substr(space_pos + 1);
    
    std::cout << "INVITE: " << client->getNickname() << " invites " << target_nick << " to " << channel_name << std::endl;
    
    // Vérifier que le channel existe
    std::map<std::string, Channel*>::iterator channel_it = _channels.find(channel_name);
    if (channel_it == _channels.end()) {
        _sendResponse(client, "403 " + client->getNickname() + " " + channel_name + " :No such channel\r\n");
        return;
    }
    
    Channel* channel = channel_it->second;
    
    // Vérifier que l'inviteur est dans le channel
    if (!channel->isMember(client)) {
        _sendResponse(client, "442 " + client->getNickname() + " " + channel_name + " :You're not on that channel\r\n");
        return;
    }
    
    // Vérifier que l'inviteur est opérateur
    if (!channel->isOperator(client)) {
        _sendResponse(client, "482 " + client->getNickname() + " " + channel_name + " :You're not channel operator\r\n");
        return;
    }
    
    // Trouver le client cible
    Client* target_client = _findClientByNickname(target_nick);
    if (target_client == NULL) {
        _sendResponse(client, "401 " + client->getNickname() + " " + target_nick + " :No such nick/channel\r\n");
        return;
    }
    
    // Vérifier que le client cible n'est pas déjà dans le channel
    if (channel->isMember(target_client)) {
        _sendResponse(client, "443 " + client->getNickname() + " " + target_nick + " " + channel_name + " :is already on channel\r\n");
        return;
    }
    
    // Envoyer l'invitation au client cible
    std::string invite_msg = ":" + client->getNickname() + " INVITE " + target_nick + " " + channel_name + "\r\n";
    target_client->appendToSendBuffer(invite_msg);
    
    // Essayer d'envoyer immédiatement
    const std::string& send_buffer = target_client->getSendBuffer();
    if (!send_buffer.empty()) {
        ssize_t bytes_sent = send(target_client->getFd(), send_buffer.c_str(), send_buffer.length(), 0);
        if (bytes_sent > 0) {
            target_client->clearSendBuffer(bytes_sent);
        }
    }
    
    // Confirmer à celui qui invite
    _sendResponse(client, "341 " + client->getNickname() + " " + target_nick + " " + channel_name + "\r\n");
    
    std::cout << "Successfully sent invitation from " << client->getNickname() 
              << " to " << target_nick << " for channel " << channel_name << std::endl;
}

// Gérer la commande TOPIC (modifier ou afficher le topic)
void Server::_handleTopicCommand(Client* client, const std::string& args) {
    std::cout << "Handling TOPIC command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        _sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        _sendResponse(client, "461 " + client->getNickname() + " TOPIC :Not enough parameters\r\n");
        return;
    }
    
    // Parser: TOPIC <channel> [:<new topic>]
    std::string channel_name;
    std::string new_topic;
    
    size_t space_pos = args.find(' ');
    if (space_pos != std::string::npos) {
        channel_name = args.substr(0, space_pos);
        std::string topic_part = args.substr(space_pos + 1);
        if (!topic_part.empty() && topic_part[0] == ':') {
            new_topic = topic_part.substr(1);
        } else {
            new_topic = topic_part;
        }
    } else {
        channel_name = args;
    }
    
    std::cout << "TOPIC: " << client->getNickname() << " for channel " << channel_name;
    if (!new_topic.empty()) {
        std::cout << ", new topic: " << new_topic;
    }
    std::cout << std::endl;
    
    // Vérifier que le channel existe
    std::map<std::string, Channel*>::iterator channel_it = _channels.find(channel_name);
    if (channel_it == _channels.end()) {
        _sendResponse(client, "403 " + client->getNickname() + " " + channel_name + " :No such channel\r\n");
        return;
    }
    
    Channel* channel = channel_it->second;
    
    // Vérifier que le client est dans le channel
    if (!channel->isMember(client)) {
        _sendResponse(client, "442 " + client->getNickname() + " " + channel_name + " :You're not on that channel\r\n");
        return;
    }
    
    if (new_topic.empty()) {
        // Afficher le topic actuel
        if (channel->getTopic().empty()) {
            _sendResponse(client, "331 " + client->getNickname() + " " + channel_name + " :No topic is set\r\n");
        } else {
            _sendResponse(client, "332 " + client->getNickname() + " " + channel_name + " :" + channel->getTopic() + "\r\n");
        }
    } else {
        // Modifier le topic
        
        // Vérifier les permissions (mode +t = topic restreint aux opérateurs)
        if (channel->isTopicRestricted() && !channel->isOperator(client)) {
            _sendResponse(client, "482 " + client->getNickname() + " " + channel_name + " :You're not channel operator\r\n");
            return;
        }
        
        // Changer le topic
        channel->setTopic(new_topic);
        
        // Broadcaster le changement à tous les membres
        std::string topic_msg = ":" + client->getNickname() + " TOPIC " + channel_name + " :" + new_topic + "\r\n";
        channel->broadcastMessage(topic_msg, NULL); // NULL = envoyer à tous
        
        std::cout << "Topic changed for " << channel_name << " by " << client->getNickname() 
                  << ": " << new_topic << std::endl;
    }
}

// Gérer la commande MODE (modifier les modes du channel)
void Server::_handleModeCommand(Client* client, const std::string& args) {
    std::cout << "Handling MODE command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        _sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        _sendResponse(client, "461 " + client->getNickname() + " MODE :Not enough parameters\r\n");
        return;
    }
    
    // Parser: MODE <channel> <modestring> [<modeparams>...]
    // Exemples: MODE #general +i
    //          MODE #general +k secret
    //          MODE #general +o alice
    //          MODE #general +l 50
    
    std::string remaining = args;
    std::string channel_name, mode_string;
    std::vector<std::string> params;
    
    // Extraire le nom du channel
    size_t space_pos = remaining.find(' ');
    if (space_pos == std::string::npos) {
        // Pas de modes spécifiés, afficher les modes actuels
        channel_name = remaining;
        // TODO: Implémenter l'affichage des modes actuels
        _sendResponse(client, "324 " + client->getNickname() + " " + channel_name + " +\r\n");
        return;
    }
    
    channel_name = remaining.substr(0, space_pos);
    remaining = remaining.substr(space_pos + 1);
    
    // Extraire le mode string
    space_pos = remaining.find(' ');
    if (space_pos == std::string::npos) {
        mode_string = remaining;
    } else {
        mode_string = remaining.substr(0, space_pos);
        remaining = remaining.substr(space_pos + 1);
        
        // Extraire les paramètres
        while (!remaining.empty()) {
            space_pos = remaining.find(' ');
            if (space_pos == std::string::npos) {
                params.push_back(remaining);
                break;
            } else {
                params.push_back(remaining.substr(0, space_pos));
                remaining = remaining.substr(space_pos + 1);
            }
        }
    }
    
    std::cout << "MODE: " << channel_name << " " << mode_string;
    for (size_t i = 0; i < params.size(); ++i) {
        std::cout << " " << params[i];
    }
    std::cout << std::endl;
    
    // Vérifier que le channel existe
    std::map<std::string, Channel*>::iterator channel_it = _channels.find(channel_name);
    if (channel_it == _channels.end()) {
        _sendResponse(client, "403 " + client->getNickname() + " " + channel_name + " :No such channel\r\n");
        return;
    }
    
    Channel* channel = channel_it->second;
    
    // Vérifier que le client est dans le channel
    if (!channel->isMember(client)) {
        _sendResponse(client, "442 " + client->getNickname() + " " + channel_name + " :You're not on that channel\r\n");
        return;
    }
    
    // Vérifier que le client est opérateur
    if (!channel->isOperator(client)) {
        _sendResponse(client, "482 " + client->getNickname() + " " + channel_name + " :You're not channel operator\r\n");
        return;
    }
    
    // Parser les modes
    bool adding = true; // true = +, false = -
    size_t param_index = 0;
    std::string applied_modes = "";
    std::string applied_params = "";
    
    for (size_t i = 0; i < mode_string.length(); ++i) {
        char mode_char = mode_string[i];
        
        if (mode_char == '+') {
            adding = true;
            continue;
        } else if (mode_char == '-') {
            adding = false;
            continue;
        }
        
        // Traiter chaque mode
        switch (mode_char) {
            case 'i': // Invite only
                channel->setInviteOnly(adding);
                applied_modes += (adding ? "+" : "-");
                applied_modes += "i";
                std::cout << "Channel " << channel_name << " invite-only: " << (adding ? "ON" : "OFF") << std::endl;
                break;
                
            case 't': // Topic restricted
                channel->setTopicRestricted(adding);
                applied_modes += (adding ? "+" : "-");
                applied_modes += "t";
                std::cout << "Channel " << channel_name << " topic restricted: " << (adding ? "ON" : "OFF") << std::endl;
                break;
                
            case 'k': // Key (password)
                if (adding) {
                    if (param_index < params.size()) {
                        channel->setKey(params[param_index]);
                        applied_modes += "+k";
                        applied_params += " " + params[param_index];
                        param_index++;
                        std::cout << "Channel " << channel_name << " key set to: " << params[param_index-1] << std::endl;
                    } else {
                        _sendResponse(client, "461 " + client->getNickname() + " MODE :Not enough parameters\r\n");
                        return;
                    }
                } else {
                    channel->setKey("");
                    applied_modes += "-k";
                    std::cout << "Channel " << channel_name << " key removed" << std::endl;
                }
                break;
                
            case 'o': // Operator privileges
                if (param_index < params.size()) {
                    Client* target_client = _findClientByNickname(params[param_index]);
                    if (target_client != NULL && channel->isMember(target_client)) {
                        if (adding) {
                            channel->addOperator(target_client);
                            applied_modes += "+o";
                        } else {
                            channel->removeOperator(target_client);
                            applied_modes += "-o";
                        }
                        applied_params += " " + params[param_index];
                        param_index++;
                    } else {
                        _sendResponse(client, "401 " + client->getNickname() + " " + params[param_index] + " :No such nick/channel\r\n");
                        return;
                    }
                } else {
                    _sendResponse(client, "461 " + client->getNickname() + " MODE :Not enough parameters\r\n");
                    return;
                }
                break;
                
            case 'l': // User limit
                if (adding) {
                    if (param_index < params.size()) {
                        int limit = std::atoi(params[param_index].c_str());
                        if (limit > 0) {
                            channel->setUserLimit(limit);
                            applied_modes += "+l";
                            applied_params += " " + params[param_index];
                            param_index++;
                            std::cout << "Channel " << channel_name << " user limit set to: " << limit << std::endl;
                        }
                    } else {
                        _sendResponse(client, "461 " + client->getNickname() + " MODE :Not enough parameters\r\n");
                        return;
                    }
                } else {
                    channel->setUserLimit(0);
                    applied_modes += "-l";
                    std::cout << "Channel " << channel_name << " user limit removed" << std::endl;
                }
                break;
                
            default:
                _sendResponse(client, "472 " + client->getNickname() + " " + mode_char + " :is unknown mode char to me\r\n");
                return;
        }
    }
    
    // Broadcaster le changement de mode à tous les membres
    if (!applied_modes.empty()) {
        std::string mode_msg = ":" + client->getNickname() + " MODE " + channel_name + " " + applied_modes + applied_params + "\r\n";
        channel->broadcastMessage(mode_msg, NULL); // NULL = envoyer à tous
        
        std::cout << "Mode changes applied for " << channel_name << ": " << applied_modes << applied_params << std::endl;
    }
}