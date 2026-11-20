#pragma once

#include <string_view>

namespace sdp::gui {

/// All user-visible strings. Additional languages can be added without touching
/// the window or tray code.
struct Strings {
    std::string_view app_name;
    std::string_view status_running;
    std::string_view status_stopped;
    std::string_view status_unavailable;
    std::string_view open_dashboard;
    std::string_view presets_menu;
    std::string_view emergency_stop;
    std::string_view exit;
    std::string_view dashboard_title;
    std::string_view state_label;
    std::string_view preset_label;
    std::string_view pid_label;
    std::string_view journal_label;
    std::string_view refresh;
    std::string_view close;
    std::string_view preset_applied;
    std::string_view stop_requested;
    std::string_view stop_failed;
    std::string_view parse_error;
};

[[nodiscard]] const Strings& english();

} // namespace sdp::gui
