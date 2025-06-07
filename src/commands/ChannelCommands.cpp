#include "../../include/commands/ChannelCommands.hpp"
#include "../../include/Server.hpp"
#include "../../include/Client.hpp"
#include "../../include/Channel.hpp"
#include <iostream>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <vector>

// Gérer la commande JOIN (rejoindre un channel)
void ChannelCommands::handleJoin(Server* server, Client* client, const std::string& args) {
    std::cout << "Handling JOIN command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        server->sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        server->sendResponse(client, "461 " + client->getNickname() + " JOIN :Not enough parameters\r\n");
        return;
    }
    
    // Parser: JOIN <channel> [<key>]
    std::string channel_name = args;
    size_t space_pos = channel_name.find(' ');
    if (space_pos != std::string::npos) {
        channel_name = channel_name.substr(0, space_pos);
    }
    
    // Vérifier que ça commence par #
    if (channel_name[0] != '#') {
        channel_name = "#" + channel_name;
    }
    
    std::cout << "Client " << client->getNickname() << " joining channel " << channel_name << std::endl;
    
    // Obtenir ou créer le channel
    Channel* channel = server->getOrCreateChannel(channel_name);
    
    // Vérifier les modes du channel
    
    // Mode +k : Vérifier le mot de passe
    if (!channel->getKey().empty()) {
        server->sendResponse(client, "475 " + client->getNickname() + " " + channel_name + " :Cannot join channel (+k)\r\n");
        return;
    }
    
    // Mode +i : Channel invitation seulement (pour l'instant on laisse passer, sera géré avec INVITE)
    if (channel->isInviteOnly()) {
        // TODO: Vérifier si le client a été invité
        server->sendResponse(client, "473 " + client->getNickname() + " " + channel_name + " :Cannot join channel (+i)\r\n");
        return;
    }
    
    // Mode +l : Vérifier la limite d'utilisateurs
    if (channel->getUserLimit() > 0 && channel->getMembers().size() >= static_cast<size_t>(channel->getUserLimit())) {
        server->sendResponse(client, "471 " + client->getNickname() + " " + channel_name + " :Cannot join channel (+l)\r\n");
        return;
    }
    
    // Le premier membre devient automatiquement opérateur
    bool is_operator = channel->getMembers().empty();
    
    // Ajouter le client au channel
    channel->addMember(client, is_operator);
    
    // Envoyer confirmation de JOIN à l'utilisateur
    std::string join_msg = ":" + client->getNickname() + " JOIN " + channel_name + "\r\n";
    server->sendResponse(client, join_msg);
    
    // Broadcaster le JOIN aux autres membres
    channel->broadcastMessage(join_msg, client);
}

// Gérer la commande KICK (éjecter un utilisateur d'un channel)
void ChannelCommands::handleKick(Server* server, Client* client, const std::string& args) {
    std::cout << "Handling KICK command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        server->sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        server->sendResponse(client, "461 " + client->getNickname() + " KICK :Not enough parameters\r\n");
        return;
    }
    
    // Parser: KICK <channel> <user> [:<reason>]
    // Exemple: KICK #general alice :Spamming
    
    std::string remaining = args;
    std::string channel_name, target_nick, reason;
    
    // Extraire le nom du channel
    size_t space_pos = remaining.find(' ');
    if (space_pos == std::string::npos) {
        server->sendResponse(client, "461 " + client->getNickname() + " KICK :Not enough parameters\r\n");
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
    Channel* channel = server->getOrCreateChannel(channel_name);
    if (channel == NULL) {
        server->sendResponse(client, "403 " + client->getNickname() + " " + channel_name + " :No such channel\r\n");
        return;
    }
    
    // Vérifier que l'utilisateur qui kick est dans le channel
    if (!channel->isMember(client)) {
        server->sendResponse(client, "442 " + client->getNickname() + " " + channel_name + " :You're not on that channel\r\n");
        return;
    }
    
    // Vérifier que l'utilisateur qui kick est opérateur
    if (!channel->isOperator(client)) {
        server->sendResponse(client, "482 " + client->getNickname() + " " + channel_name + " :You're not channel operator\r\n");
        return;
    }
    
    // Trouver l'utilisateur cible
    Client* target_client = server->findClientByNickname(target_nick);
    if (target_client == NULL) {
        server->sendResponse(client, "401 " + client->getNickname() + " " + target_nick + " :No such nick/channel\r\n");
        return;
    }
    
    // Vérifier que l'utilisateur cible est dans le channel
    if (!channel->isMember(target_client)) {
        server->sendResponse(client, "441 " + client->getNickname() + " " + target_nick + " " + channel_name + " :They aren't on that channel\r\n");
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
    server->removeEmptyChannel(channel_name);
}

// Gérer la commande INVITE (inviter un client au channel)
void ChannelCommands::handleInvite(Server* server, Client* client, const std::string& args) {
    std::cout << "Handling INVITE command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        server->sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        server->sendResponse(client, "461 " + client->getNickname() + " INVITE :Not enough parameters\r\n");
        return;
    }
    
    // Parser: INVITE <nickname> <channel>
    size_t space_pos = args.find(' ');
    if (space_pos == std::string::npos) {
        server->sendResponse(client, "461 " + client->getNickname() + " INVITE :Not enough parameters\r\n");
        return;
    }
    
    std::string target_nick = args.substr(0, space_pos);
    std::string channel_name = args.substr(space_pos + 1);
    
    std::cout << "INVITE: " << client->getNickname() << " invites " << target_nick << " to " << channel_name << std::endl;
    
    // Vérifier que le channel existe
    Channel* channel = server->getOrCreateChannel(channel_name);
    if (channel == NULL) {
        server->sendResponse(client, "403 " + client->getNickname() + " " + channel_name + " :No such channel\r\n");
        return;
    }
    
    // Vérifier que l'inviteur est dans le channel
    if (!channel->isMember(client)) {
        server->sendResponse(client, "442 " + client->getNickname() + " " + channel_name + " :You're not on that channel\r\n");
        return;
    }
    
    // Vérifier que l'inviteur est opérateur
    if (!channel->isOperator(client)) {
        server->sendResponse(client, "482 " + client->getNickname() + " " + channel_name + " :You're not channel operator\r\n");
        return;
    }
    
    // Trouver le client cible
    Client* target_client = server->findClientByNickname(target_nick);
    if (target_client == NULL) {
        server->sendResponse(client, "401 " + client->getNickname() + " " + target_nick + " :No such nick/channel\r\n");
        return;
    }
    
    // Vérifier que le client cible n'est pas déjà dans le channel
    if (channel->isMember(target_client)) {
        server->sendResponse(client, "443 " + client->getNickname() + " " + target_nick + " " + channel_name + " :is already on channel\r\n");
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
    server->sendResponse(client, "341 " + client->getNickname() + " " + target_nick + " " + channel_name + "\r\n");
    
    std::cout << "Successfully sent invitation from " << client->getNickname() 
              << " to " << target_nick << " for channel " << channel_name << std::endl;
}

// Gérer la commande TOPIC (modifier ou afficher le topic)
void ChannelCommands::handleTopic(Server* server, Client* client, const std::string& args) {
    std::cout << "Handling TOPIC command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        server->sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        server->sendResponse(client, "461 " + client->getNickname() + " TOPIC :Not enough parameters\r\n");
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
    Channel* channel = server->getOrCreateChannel(channel_name);
    if (channel == NULL) {
        server->sendResponse(client, "403 " + client->getNickname() + " " + channel_name + " :No such channel\r\n");
        return;
    }
    
    // Vérifier que le client est dans le channel
    if (!channel->isMember(client)) {
        server->sendResponse(client, "442 " + client->getNickname() + " " + channel_name + " :You're not on that channel\r\n");
        return;
    }
    
    if (new_topic.empty()) {
        // Afficher le topic actuel
        if (channel->getTopic().empty()) {
            server->sendResponse(client, "331 " + client->getNickname() + " " + channel_name + " :No topic is set\r\n");
        } else {
            server->sendResponse(client, "332 " + client->getNickname() + " " + channel_name + " :" + channel->getTopic() + "\r\n");
        }
    } else {
        // Modifier le topic
        
        // Vérifier les permissions (mode +t = topic restreint aux opérateurs)
        if (channel->isTopicRestricted() && !channel->isOperator(client)) {
            server->sendResponse(client, "482 " + client->getNickname() + " " + channel_name + " :You're not channel operator\r\n");
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
void ChannelCommands::handleMode(Server* server, Client* client, const std::string& args) {
    std::cout << "Handling MODE command for " << client->getNickname() << std::endl;
    
    // Vérifier que le client est authentifié
    if (!client->isAuthenticated()) {
        server->sendResponse(client, "451 * :You have not registered\r\n");
        return;
    }
    
    if (args.empty()) {
        server->sendResponse(client, "461 " + client->getNickname() + " MODE :Not enough parameters\r\n");
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
        server->sendResponse(client, "324 " + client->getNickname() + " " + channel_name + " +\r\n");
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
    Channel* channel = server->getOrCreateChannel(channel_name);
    if (channel == NULL) {
        server->sendResponse(client, "403 " + client->getNickname() + " " + channel_name + " :No such channel\r\n");
        return;
    }
    
    // Vérifier que le client est dans le channel
    if (!channel->isMember(client)) {
        server->sendResponse(client, "442 " + client->getNickname() + " " + channel_name + " :You're not on that channel\r\n");
        return;
    }
    
    // Vérifier que le client est opérateur
    if (!channel->isOperator(client)) {
        server->sendResponse(client, "482 " + client->getNickname() + " " + channel_name + " :You're not channel operator\r\n");
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
                        server->sendResponse(client, "461 " + client->getNickname() + " MODE :Not enough parameters\r\n");
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
                    Client* target_client = server->findClientByNickname(params[param_index]);
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
                        server->sendResponse(client, "401 " + client->getNickname() + " " + params[param_index] + " :No such nick/channel\r\n");
                        return;
                    }
                } else {
                    server->sendResponse(client, "461 " + client->getNickname() + " MODE :Not enough parameters\r\n");
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
                        server->sendResponse(client, "461 " + client->getNickname() + " MODE :Not enough parameters\r\n");
                        return;
                    }
                } else {
                    channel->setUserLimit(0);
                    applied_modes += "-l";
                    std::cout << "Channel " << channel_name << " user limit removed" << std::endl;
                }
                break;
                
            default:
                server->sendResponse(client, "472 " + client->getNickname() + " " + mode_char + " :is unknown mode char to me\r\n");
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