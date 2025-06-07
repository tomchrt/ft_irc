#ifndef MESSAGECOMMANDS_HPP
#define MESSAGECOMMANDS_HPP

#include <string>

// Forward declarations
class Server;
class Client;

class MessageCommands {
public:
    static void handlePrivmsg(Server* server, Client* client, const std::string& args);
};

#endif 