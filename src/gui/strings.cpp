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

        "Find a working strategy",
        "Edit blacklist",
        "Edit DNS resolver",
        "Advanced arguments",
        "Strategy scanner",
        "URLs (comma separated)",
        "URL file",
        "Scan level",
        "Protocols",
        "IP version",
        "Timeout (seconds)",
        "DNS address (optional)",
        "Start scan",
        "Scan progress",
        "No result yet",
        "Apply recommended strategy",
        "The scanner needs Administrator rights. Approve the elevation prompt.",
        "The installer script was modified. Reinstall ShadowDPI before scanning.",
        "Wait for the current scan to finish.",
        "Blacklist",
        "One domain per line. Changes apply after a service restart.",
        "Save",
        "DNS resolver",
        "IPv4 address",
        "IPv6 address",
        "Advanced arguments",
        "Enter CLI arguments, for example: -9 --blacklist C:\\blocked.txt",
        "Apply",
    };
    return strings;
}

} // namespace sdp::gui
