#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <sstream>

// Fonction utilitaire C++98 pour convertir int en string
inline std::string intToString(int value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

#endif 