#include "Channel.hpp"
#include "Client.hpp"
#include <algorithm>
#include <iostream>
#include <sys/socket.h>  // Pour send()
#include <unistd.h>      // Pour ssize_t

// Constructeur : créer un nouveau channel
Channel::Channel(const std::string& name) 
    : _name(name), _topic(""), _invite_only(false), _topic_restricted(false), _key(""), _user_limit(0) {
    
    std::cout << "Creating new channel: " << _name << std::endl;
}

// Destructeur
Channel::~Channel() {
    std::cout << "Destroying channel: " << _name << std::endl;
}

// Ajouter un membre au channel
void Channel::addMember(Client* client, bool is_operator) {
    // Vérifier si déjà membre
    if (isMember(client)) {
        std::cout << "Client " << client->getNickname() << " already in channel " << _name << std::endl;
        return;
    }
    
    // Vérifier la limite d'utilisateurs
    if (_user_limit > 0 && _members.size() >= static_cast<size_t>(_user_limit)) {
        std::cout << "Channel " << _name << " is full (limit: " << _user_limit << ")" << std::endl;
        return; // Channel plein
    }
    
    // Ajouter à la liste des membres
    _members.push_back(client);
    
    // Définir les droits d'opérateur
    _operators[client] = is_operator;
    
    // Ajouter le channel à la liste du client
    client->joinChannel(_name);
    
    std::cout << "Client " << client->getNickname() << " joined channel " << _name;
    if (is_operator) {
        std::cout << " (operator)";
    }
    std::cout << std::endl;
}

// Supprimer un membre du channel
void Channel::removeMember(Client* client) {
    // Chercher le client dans la liste
    std::vector<Client*>::iterator it = std::find(_members.begin(), _members.end(), client);
    
    if (it != _members.end()) {
        // Supprimer de la liste des membres
        _members.erase(it);
        
        // Supprimer des opérateurs
        _operators.erase(client);
        
        // Supprimer le channel de la liste du client
        client->leaveChannel(_name);
        
        std::cout << "Client " << client->getNickname() << " left channel " << _name << std::endl;
    } else {
        std::cout << "Client " << client->getNickname() << " was not in channel " << _name << std::endl;
    }
}

// Vérifier si un client est membre du channel
bool Channel::isMember(Client* client) const {
    return std::find(_members.begin(), _members.end(), client) != _members.end();
}

// Vérifier si un client est opérateur du channel
bool Channel::isOperator(Client* client) const {
    std::map<Client*, bool>::const_iterator it = _operators.find(client);
    return (it != _operators.end() && it->second);
}

// Diffuser un message à tous les membres du channel
void Channel::broadcastMessage(const std::string& message, Client* sender) {
    std::cout << "Broadcasting to channel " << _name << ": " << message;
    
    // Envoyer à tous les membres (sauf l'expéditeur si spécifié)
    for (std::vector<Client*>::iterator it = _members.begin(); it != _members.end(); ++it) {
        Client* member = *it;
        
        // Si sender est NULL, envoyer à tous
        // Si sender est spécifié, ne pas renvoyer le message à l'expéditeur
        if (sender == NULL || member != sender) {
            member->appendToSendBuffer(message);
            
            // Essayer d'envoyer immédiatement
            const std::string& send_buffer = member->getSendBuffer();
            if (!send_buffer.empty()) {
                ssize_t bytes_sent = send(member->getFd(), send_buffer.c_str(), send_buffer.length(), 0);
                
                if (bytes_sent > 0) {
                    member->clearSendBuffer(bytes_sent);
                }
            }
        }
    }
}

// Ajouter un opérateur
void Channel::addOperator(Client* client) {
    if (isMember(client)) {
        _operators[client] = true;
        std::cout << "Client " << client->getNickname() << " is now operator of " << _name << std::endl;
    }
}

// Retirer un opérateur
void Channel::removeOperator(Client* client) {
    if (isMember(client)) {
        _operators[client] = false;
        std::cout << "Client " << client->getNickname() << " is no longer operator of " << _name << std::endl;
    }
} 