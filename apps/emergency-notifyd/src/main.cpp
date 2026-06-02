#include "emergency/event.hpp"
#include "emergency/logger.hpp"

#ifdef ENABLE_UBUS
extern "C" int emergency_notifyd_run_ubus(void);
#endif

namespace {

emergency::EmergencyEvent makeTestEvent() {
    return emergency::EmergencyEvent{
        "fire",
        "critical",
        "notifyd-test",
        "Hardcoded fire alarm event",
    };
}

bool validateEvent(const emergency::EmergencyEvent& event) {
    return !event.type.empty() &&
           !event.severity.empty() &&
           !event.source.empty() &&
           !event.message.empty();
}

bool processEvent(const emergency::EmergencyEvent& event) {
    emergency::logInfo("received " + emergency::formatEvent(event));

    if (!validateEvent(event)) {
        emergency::logError("event validation failed");
        return false;
    }

    emergency::logInfo("event processed successfully");
    return true;
}

} // namespace

int main() {
    emergency::logInfo("emergency-notifyd starting");

#ifdef ENABLE_UBUS
    emergency::logInfo("ubus integration is enabled in this build");
    return emergency_notifyd_run_ubus();
#else
    emergency::logInfo("ubus integration is disabled in this build");

    if (!processEvent(makeTestEvent())) {
        return 1;
    }

    return 0;
#endif
}
