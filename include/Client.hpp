#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>

class Client {
private:
    // Informations de connexion
    int _fd;                        // File descriptor du socket client
    std::string _ip_address;        // Adresse IP du client
    
    // Buffer de communication
    std::string _receive_buffer;    // Buffer pour données reçues (partielles)
    std::string _send_buffer;       // Buffer pour données à envoyer
    
    // Informations IRC du client
    std::string _nickname;          // Pseudonyme IRC
    std::string _username;          // Nom d'utilisateur
    std::string _realname;          // Nom réel
    std::string _hostname;          // Hostname du client
    
    // État d'authentification
    bool _password_ok;              // A fourni le bon password ?
    bool _registered;               // A complété NICK + USER ?
    bool _authenticated;            // Complètement connecté ?
    
    // Channels auxquels le client appartient
    std::vector<std::string> _channels;

public:
    // Constructeur/Destructeur
    Client(int fd, const std::string& ip);
    ~Client();
    
    // Getters
    int getFd() const { return _fd; }
    const std::string& getNickname() const { return _nickname; }
    const std::string& getUsername() const { return _username; }
    const std::string& getRealname() const { return _realname; }
    const std::string& getHostname() const { return _hostname; }
    const std::string& getIpAddress() const { return _ip_address; }
    
    // État
    bool isPasswordOk() const { return _password_ok; }
    bool isRegistered() const { return _registered; }
    bool isAuthenticated() const { return _authenticated; }
    
    // Setters
    void setPasswordOk(bool ok) { _password_ok = ok; }
    void setNickname(const std::string& nick);
    void setUsername(const std::string& user) { _username = user; }
    void setRealname(const std::string& real) { _realname = real; }
    void setHostname(const std::string& host) { _hostname = host; }
    
    // Gestion des buffers
    void appendToReceiveBuffer(const std::string& data);
    std::string extractMessage();               // Extraire un message complet
    bool hasCompleteMessage() const;            // Y a-t-il un message complet ?
    
    void appendToSendBuffer(const std::string& data);
    const std::string& getSendBuffer() const { return _send_buffer; }
    void clearSendBuffer(size_t bytes_sent);    // Supprimer les bytes envoyés
    bool hasPendingData() const { return !_send_buffer.empty(); }
    
    // Gestion des channels
    void joinChannel(const std::string& channel);
    void leaveChannel(const std::string& channel);
    bool isInChannel(const std::string& channel) const;
    const std::vector<std::string>& getChannels() const { return _channels; }
    
private:
    void _updateRegistrationStatus();          // Vérifier si NICK+USER complets
};

#endif 