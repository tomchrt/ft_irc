#ifndef CHANNELCOMMANDS_HPP
#define CHANNELCOMMANDS_HPP

#include <string>

// Forward declarations
class Server;
class Client;

class ChannelCommands {
public:
    static void handleJoin(Server* server, Client* client, const std::string& args);
    static void handleKick(Server* server, Client* client, const std::string& args);
    static void handleInvite(Server* server, Client* client, const std::string& args);
    static void handleTopic(Server* server, Client* client, const std::string& args);
    static void handleMode(Server* server, Client* client, const std::string& args);
};

#endif 