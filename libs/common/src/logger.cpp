#include "emergency/logger.hpp"

#include <iostream>

namespace emergency {

void logInfo(const std::string& message) {
    std::cout << "[INFO] " << message << std::endl;
}

void logError(const std::string& message) {
    std::cerr << "[ERROR] " << message << std::endl;
}

} // namespace emergency
