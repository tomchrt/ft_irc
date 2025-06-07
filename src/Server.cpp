#include "Server.hpp"
#include "Client.hpp"
#include "Channel.hpp"
#include "commands/AuthCommands.hpp"
#include "commands/ChannelCommands.hpp"
#include "commands/MessageCommands.hpp"
#include <stdexcept>
#include <cstring>    // pour strerror
#include <cerrno>     // pour errno
#include "utils.hpp"  // pour intToString

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
    
    // Supprimer tous les channels
    for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
        delete it->second;
    }
    _channels.clear();
    
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
    int opt = 1;
    if (setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("Failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
    }
    
    // Configurer le socket en mode non-bloquant
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
        throw std::runtime_error("Failed to bind to port " + intToString(_port) + ": " + std::string(strerror(errno)));
    }
    
    // Mettre le socket en mode écoute
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
        int poll_result = poll(&_poll_fds[0], _poll_fds.size(), -1);
        
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue; // Interrompu par un signal (normal), continuer
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
    client->appendToReceiveBuffer(std::string(buffer));
    
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