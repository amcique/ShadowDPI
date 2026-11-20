#include "strings.hpp"

namespace sdp::gui {

const Strings& english()
{
    static const Strings strings{
        "ShadowDPI",
        "Running",
        "Stopped",
        "Service unavailable",
        "Open dashboard",
        "Presets",
        "Emergency stop",
        "Exit",
        "ShadowDPI dashboard",
        "State:",
        "Preset:",
        "PID:",
        "Journal",
        "Refresh",
        "Close",
        "Preset applied. Restart the service for it to take effect.",
        "Stop requested.",
        "The service refused the stop request.",
        "The service returned unreadable data.",
    };
    return strings;
}

} // namespace sdp::gui
