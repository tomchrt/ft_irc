#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>
#include <map>

// Forward declaration
class Client;

class Channel {
private:
    std::string _name;                          // Nom du channel (#general)
    std::string _topic;                         // Topic du channel
    std::vector<Client*> _members;              // Liste des membres
    std::map<Client*, bool> _operators;         // Client -> est operator ?
    
    // Modes du channel
    bool _invite_only;                          // Mode +i
    bool _topic_restricted;                     // Mode +t
    std::string _key;                           // Mode +k (password)
    int _user_limit;                            // Mode +l (0 = pas de limite)

public:
    // Constructeur
    Channel(const std::string& name);
    ~Channel();
    
    // Getters
    const std::string& getName() const { return _name; }
    const std::string& getTopic() const { return _topic; }
    const std::vector<Client*>& getMembers() const { return _members; }
    
    // Gestion des membres
    void addMember(Client* client, bool is_operator = false);
    void removeMember(Client* client);
    bool isMember(Client* client) const;
    bool isOperator(Client* client) const;
    bool isEmpty() const { return _members.empty(); }
    
    // Broadcast de messages
    void broadcastMessage(const std::string& message, Client* sender = NULL);
    
    // Gestion des modes
    void setTopic(const std::string& topic) { _topic = topic; }
    bool isInviteOnly() const { return _invite_only; }
    bool isTopicRestricted() const { return _topic_restricted; }
    const std::string& getKey() const { return _key; }
    int getUserLimit() const { return _user_limit; }
    
    void setInviteOnly(bool invite_only) { _invite_only = invite_only; }
    void setTopicRestricted(bool topic_restricted) { _topic_restricted = topic_restricted; }
    void setKey(const std::string& key) { _key = key; }
    void setUserLimit(int limit) { _user_limit = limit; }
    
    // Gestion des opérateurs
    void addOperator(Client* client);
    void removeOperator(Client* client);
};

#endif 