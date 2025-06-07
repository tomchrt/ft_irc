#ifndef AUTHCOMMANDS_HPP
#define AUTHCOMMANDS_HPP

#include <string>

// Forward declarations
class Server;
class Client;

class AuthCommands {
public:
    static void handlePass(Server* server, Client* client, const std::string& args);
    static void handleNick(Server* server, Client* client, const std::string& args);
    static void handleUser(Server* server, Client* client, const std::string& args);
    
private:
    static void sendWelcomeMessages(Server* server, Client* client);
};

#endif 