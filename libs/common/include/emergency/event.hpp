#pragma once

#include <string>

namespace emergency {

struct EmergencyEvent {
    std::string type;
    std::string severity;
    std::string source;
    std::string message;
};

std::string formatEvent(const EmergencyEvent& event);

} // namespace emergency
