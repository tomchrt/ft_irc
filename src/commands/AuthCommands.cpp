#include "../../include/commands/AuthCommands.hpp"
#include "../../include/Server.hpp"
#include "../../include/Client.hpp"
#include "../../include/utils.hpp"
#include <iostream>

// Gérer la commande PASS (authentification password)
void AuthCommands::handlePass(Server* server, Client* client, const std::string& args) {
    std::cout << "Handling PASS command for client " << client->getFd() << std::endl;
    
    if (args.empty()) {
        server->sendResponse(client, "461 * PASS :Not enough parameters\r\n");
        return;
    }
    
    // Comparer avec le mot de passe du serveur
    if (args == server->getPassword()) {
        client->setPasswordOk(true);
        std::cout << "Client " << client->getFd() << " provided correct password" << std::endl;
        // Pas de réponse immédiate pour PASS selon RFC 1459
    } else {
        std::cout << "Client " << client->getFd() << " provided wrong password" << std::endl;
        server->sendResponse(client, "464 * :Password incorrect\r\n");
    }
}

// Gérer la commande NICK (définir nickname)
void AuthCommands::handleNick(Server* server, Client* client, const std::string& args) {
    std::cout << "Handling NICK command for client " << client->getFd() << std::endl;
    
    if (args.empty()) {
        server->sendResponse(client, "431 * :No nickname given\r\n");
        return;
    }
    
    // TODO: Vérifier si le nickname est déjà utilisé par un autre client
    // TODO: Vérifier si le nickname est valide (pas de caractères interdits)
    
    std::string old_nick = client->getNickname();
    client->setNickname(args);
    
    std::cout << "Client " << client->getFd() << " nickname set to: " << args << std::endl;
    
    // Si le client est maintenant complètement enregistré, envoyer les messages de bienvenue
    if (client->isAuthenticated()) {
        sendWelcomeMessages(server, client);
    }
}

// Gérer la commande USER (définir username et realname)
void AuthCommands::handleUser(Server* server, Client* client, const std::string& args) {
    std::cout << "Handling USER command for client " << client->getFd() << std::endl;
    
    if (args.empty()) {
        server->sendResponse(client, "461 * USER :Not enough parameters\r\n");
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
        sendWelcomeMessages(server, client);
    }
}

// Envoyer les messages de bienvenue IRC
void AuthCommands::sendWelcomeMessages(Server* server, Client* client) {
    std::cout << "Sending welcome messages to " << client->getNickname() << std::endl;
    
    std::string nick = client->getNickname();
    
    // 001 RPL_WELCOME
    server->sendResponse(client, "001 " + nick + " :Welcome to the Internet Relay Network " + nick + "\r\n");
    
    // 002 RPL_YOURHOST  
    server->sendResponse(client, "002 " + nick + " :Your host is localhost, running version 1.0\r\n");
    
    // 003 RPL_CREATED
    server->sendResponse(client, "003 " + nick + " :This server was created today\r\n");
    
    // 004 RPL_MYINFO
    server->sendResponse(client, "004 " + nick + " localhost 1.0 o o\r\n");
} 