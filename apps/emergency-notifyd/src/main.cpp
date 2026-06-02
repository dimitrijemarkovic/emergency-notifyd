#include "emergency/event.hpp"
#include "emergency/logger.hpp"

namespace {

emergency::EmergencyEvent makeTestEvent() {
    return emergency::EmergencyEvent{
        "fire",
        "critical",
        "notifyd-test",
        "Hardcoded fire alarm event",
    };
}

void processEvent(const emergency::EmergencyEvent& event) {
    emergency::logInfo("received " + emergency::formatEvent(event));
}

} // namespace

int main() {
    emergency::logInfo("emergency-notifyd starting");
    emergency::logInfo("ubus integration is disabled in this build");
    processEvent(makeTestEvent());

    return 0;
}
