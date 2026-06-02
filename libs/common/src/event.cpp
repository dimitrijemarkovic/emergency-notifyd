#include "emergency/event.hpp"

#include <sstream>

namespace emergency {

std::string formatEvent(const EmergencyEvent& event) {
    std::ostringstream output;
    output << "event{"
           << "type=" << event.type
           << ", severity=" << event.severity
           << ", source=" << event.source
           << ", message=\"" << event.message << "\""
           << "}";
    return output.str();
}

} // namespace emergency
