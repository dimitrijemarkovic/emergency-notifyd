#include "emergency/logger.hpp"

int main() {
    emergency::logInfo("emergency-mqtt-service starting");
    emergency::logInfo("ubus integration is disabled in this build");
    return 0;
}
