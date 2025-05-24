#include "Client.hpp"
#include <algorithm>
#include <iostream>

// Constructeur : initialise un nouveau client
Client::Client(int fd, const std::string& ip) 
    : _fd(fd), _ip_address(ip), _password_ok(false), _registered(false), _authenticated(false) {
    
    std::cout << "Creating new client object for fd " << _fd << " from " << _ip_address << std::endl;
    
    // Initialiser les buffers vides
    _receive_buffer.clear();
    _send_buffer.clear();
    
    // Pas encore de nickname/username définis
    _nickname.clear();
    _username.clear();
    _realname.clear();
    _hostname = _ip_address; // Par défaut, hostname = IP
    
    // Pas encore dans de channels
    _channels.clear();
}

// Destructeur : nettoyer les ressources
Client::~Client() {
    std::cout << "Destroying client object for " << _nickname 
              << " (fd: " << _fd << ")" << std::endl;
    
    // Les buffers et vectors se nettoient automatiquement
    // Le socket sera fermé par la classe Server
}

// Définir le nickname et vérifier l'état d'enregistrement
void Client::setNickname(const std::string& nick) {
    _nickname = nick;
    std::cout << "Client " << _fd << " set nickname to: " << _nickname << std::endl;
    
    // Vérifier si maintenant complètement enregistré
    _updateRegistrationStatus();
}

// Ajouter des données au buffer de réception
void Client::appendToReceiveBuffer(const std::string& data) {
    _receive_buffer += data;
    
    std::cout << "Added " << data.length() << " bytes to receive buffer for client " 
              << _fd << " (total: " << _receive_buffer.length() << " bytes)" << std::endl;
}

// Extraire un message complet du buffer de réception
std::string Client::extractMessage() {
    // Chercher le délimiteur de fin de ligne IRC (\n)
    size_t pos = _receive_buffer.find('\n');
    
    if (pos == std::string::npos) {
        return ""; // Pas de message complet disponible
    }
    
    // Extraire le message jusqu'au \n (inclus)
    std::string message = _receive_buffer.substr(0, pos);
    
    // Supprimer ce message du buffer (y compris le \n)
    _receive_buffer.erase(0, pos + 1);
    
    // Supprimer le \r s'il est présent à la fin du message
    if (!message.empty() && message[message.length() - 1] == '\r') {
        message.erase(message.length() - 1);
    }
    
    std::cout << "Extracted message from client " << _fd << ": '" << message << "'" << std::endl;
    
    return message;
}

// Vérifier s'il y a au moins un message complet dans le buffer
bool Client::hasCompleteMessage() const {
    return _receive_buffer.find('\n') != std::string::npos;
}

// Ajouter des données au buffer d'envoi
void Client::appendToSendBuffer(const std::string& data) {
    _send_buffer += data;
    
    std::cout << "Added " << data.length() << " bytes to send buffer for client " 
              << _fd << " (total: " << _send_buffer.length() << " bytes)" << std::endl;
}

// Supprimer les bytes déjà envoyés du buffer d'envoi
void Client::clearSendBuffer(size_t bytes_sent) {
    if (bytes_sent >= _send_buffer.length()) {
        // Tout a été envoyé
        _send_buffer.clear();
    } else {
        // Supprimer seulement les bytes envoyés
        _send_buffer.erase(0, bytes_sent);
    }
    
    std::cout << "Cleared " << bytes_sent << " bytes from send buffer for client " 
              << _fd << " (remaining: " << _send_buffer.length() << " bytes)" << std::endl;
}

// Rejoindre un channel
void Client::joinChannel(const std::string& channel) {
    // Vérifier si déjà dans ce channel
    if (isInChannel(channel)) {
        std::cout << "Client " << _nickname << " already in channel " << channel << std::endl;
        return;
    }
    
    // Ajouter le channel à la liste
    _channels.push_back(channel);
    
    std::cout << "Client " << _nickname << " joined channel " << channel << std::endl;
}

// Quitter un channel
void Client::leaveChannel(const std::string& channel) {
    // Chercher le channel dans la liste
    std::vector<std::string>::iterator it = std::find(_channels.begin(), _channels.end(), channel);
    
    if (it != _channels.end()) {
        // Channel trouvé, le supprimer
        _channels.erase(it);
        std::cout << "Client " << _nickname << " left channel " << channel << std::endl;
    } else {
        std::cout << "Client " << _nickname << " was not in channel " << channel << std::endl;
    }
}

// Vérifier si le client est dans un channel spécifique
bool Client::isInChannel(const std::string& channel) const {
    return std::find(_channels.begin(), _channels.end(), channel) != _channels.end();
}

// Vérifier l'état d'enregistrement (NICK + USER complets)
void Client::_updateRegistrationStatus() {
    // Pour être enregistré, il faut avoir un nickname ET un username non vides
    bool was_registered = _registered;
    _registered = (!_nickname.empty() && !_username.empty());
    
    // Si le statut a changé, mettre à jour l'authentification complète
    if (_registered && !was_registered) {
        std::cout << "Client " << _fd << " is now registered (nick: " 
                  << _nickname << ", user: " << _username << ")" << std::endl;
        
        // Si aussi le password est OK, alors complètement authentifié
        if (_password_ok) {
            _authenticated = true;
            std::cout << "Client " << _nickname << " is now fully authenticated" << std::endl;
        }
    } else if (_registered && _password_ok && !_authenticated) {
        // Cas où le password était déjà OK avant l'enregistrement
        _authenticated = true;
        std::cout << "Client " << _nickname << " is now fully authenticated" << std::endl;
    }
} 