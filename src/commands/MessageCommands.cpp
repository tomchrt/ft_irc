#include "../../include/commands/MessageCommands.hpp"
#include "../../include/Server.hpp"
#include "../../include/Client.hpp"
#include "../../include/Channel.hpp"
#include <iostream>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>

// Gérer la commande PRIVMSG (envoyer un message)
void MessageCommands::handlePrivmsg(Server* server, Client* client, const std::string& args) {
    std::cout << "Handling PRIVMSG command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        server->sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        server->sendResponse(client, "461 " + client->getNickname() + " PRIVMSG :Not enough parameters\r\n");
        return;
    }
    
    // Parser: PRIVMSG <target> :<message>
    size_t space_pos = args.find(' ');
    if (space_pos == std::string::npos) {
        server->sendResponse(client, "461 " + client->getNickname() + " PRIVMSG :Not enough parameters\r\n");
        return;
    }
    
    std::string target = args.substr(0, space_pos);
    std::string message_part = args.substr(space_pos + 1);
    
    if (message_part.empty() || message_part[0] != ':') {
        server->sendResponse(client, "461 " + client->getNickname() + " PRIVMSG :Not enough parameters\r\n");
        return;
    }
    
    std::string message = message_part.substr(1); // Enlever le ':'
    
    std::cout << "PRIVMSG from " << client->getNickname() << " to " << target << ": " << message << std::endl;
    
    // Construire le message IRC à envoyer
    std::string irc_message = ":" + client->getNickname() + " PRIVMSG " + target + " :" + message + "\r\n";
    
    // Vérifier si c'est un channel (commence par #)
    if (target[0] == '#') {
        // Message vers un channel - utiliser la méthode publique
        Channel* channel = server->getOrCreateChannel(target);
        if (channel != NULL) {
            if (channel->isMember(client)) {
                // Broadcaster le message aux autres membres du channel
                channel->broadcastMessage(irc_message, client);
            } else {
                server->sendResponse(client, "404 " + client->getNickname() + " " + target + " :Cannot send to channel\r\n");
            }
        } else {
            server->sendResponse(client, "403 " + client->getNickname() + " " + target + " :No such channel\r\n");
        }
    } else {
        // Message privé vers un utilisateur
        Client* target_client = server->findClientByNickname(target);
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
            server->sendResponse(client, "401 " + client->getNickname() + " " + target + " :No such nick/channel\r\n");
        }
    }
} 