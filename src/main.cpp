#include "Server.hpp"
#include <iostream>
#include <exception>
#include <cstdlib>    // Pour strtol

// Constantes pour les vérifications
#define PORT_ARG_INDEX 1
#define PASSWORD_ARG_INDEX 2
#define MAX_UINT16_BITS 65535

int main(int argc, char **argv) {
    // Vérification des arguments de la ligne de commande
    // argc = nombre d'arguments, argv = tableau des arguments
    if (argc != 3) {
        // Si pas exactement 3 arguments (programme + port + password)
        std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1; // Code d'erreur pour indiquer une utilisation incorrecte
    }
    
    try {
        // === CONVERSION ROBUSTE DU PORT ===
        // Utilisation de strtol pour une conversion sécurisée
        char* endptr;
        long port = std::strtol(argv[PORT_ARG_INDEX], &endptr, 10);
        
        // Vérifications complètes :
        // 1. *endptr != '\0' : vérifier qu'il n'y a pas de caractères non-numériques
        // 2. port <= 0 : vérifier que le port est positif
        // 3. port > MAX_UINT16_BITS : vérifier que le port est dans la plage valide
        if (*endptr != '\0' || port <= 0 || port > MAX_UINT16_BITS) {
            std::cerr << "Error: Invalid port number. Must be 1-65535" << std::endl;
            return 1;
        }
        
        // Vérification range utilisateur (ports < 1024 nécessitent des privilèges root)
        if (port < 1024) {
            std::cerr << "Warning: Port < 1024 requires root privileges" << std::endl;
        }
        
        // === VÉRIFICATION PASSWORD ===
        // argv[2] est le deuxième argument (le password)
        std::string password = argv[PASSWORD_ARG_INDEX];
        
        // Vérification que le password n'est pas vide
        if (password.empty()) {
            std::cerr << "Error: Password cannot be empty" << std::endl;
            return 1;
        }
        
        // Conversion safe vers int (on a vérifié les limites)
        int server_port = static_cast<int>(port);
        
        std::cout << "Starting IRC Server..." << std::endl;
        std::cout << "Port: " << server_port << std::endl;
        std::cout << "Password: " << password << std::endl;
        
        // Créer l'instance du serveur IRC avec les paramètres
        Server ircServer(server_port, password);
        
        // Démarrer le serveur (boucle infinie jusqu'à interruption)
        ircServer.start();
        
    } catch (const std::exception& e) {
        // Capturer toutes les exceptions pour éviter les crashes
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        // Capturer toute autre exception non standard
        std::cerr << "Unknown server error occurred" << std::endl;
        return 1;
    }
    
    std::cout << "Server shutdown complete" << std::endl;
    return 0; // Succès
}
