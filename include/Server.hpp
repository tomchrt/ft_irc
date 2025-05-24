#ifndef SERVER_HPP
#define SERVER_HPP

#include <iostream>
#include <string>
#include <vector>
#include <map>

// Headers système pour les sockets (Unix/Linux)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

// Forward declarations pour éviter les inclusions circulaires
class Client;

class Server {
private:
    // Configuration du serveur
    int _port;                              // Port d'écoute
    std::string _password;                  // Mot de passe du serveur
    
    // Socket et réseau
    int _server_fd;                         // File descriptor du socket serveur
    struct sockaddr_in _server_addr;        // Adresse du serveur
    
    // Gestion des clients
    std::vector<struct pollfd> _poll_fds;   // Array pour poll()
    std::map<int, Client*> _clients;        // Map fd -> Client*
    
    // État du serveur
    bool _running;                          // Serveur en marche ?

public:
    // Constructeur/Destructeur
    Server(int port, const std::string& password);
    ~Server();
    
    // Méthodes principales
    void start();                           // Démarrer le serveur
    void stop();                            // Arrêter le serveur
    
private:
    // Méthodes d'initialisation
    void _setupSocket();                    // Créer et configurer le socket
    void _bindAndListen();                  // Bind + listen
    
    // Boucle principale
    void _runEventLoop();                   // Boucle poll() principale
    
    // Gestion des connexions
    void _acceptNewClient();                // Accepter nouvelle connexion
    void _handleClientData(int client_fd);  // Traiter données d'un client
    void _disconnectClient(int client_fd);  // Déconnecter un client
    
    // Utilitaires
    void _addToPoll(int fd, short events);  // Ajouter FD à poll()
    void _removeFromPoll(int fd);           // Supprimer FD de poll()
};

#endif
