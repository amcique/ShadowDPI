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

    std::string_view scan_menu;
    std::string_view blacklist_menu;
    std::string_view dns_menu;
    std::string_view advanced_menu;
    std::string_view scan_title;
    std::string_view urls_label;
    std::string_view url_file_label;
    std::string_view level_label;
    std::string_view protocols_label;
    std::string_view ip_label;
    std::string_view timeout_label;
    std::string_view dns_label;
    std::string_view start_scan;
    std::string_view scan_progress;
    std::string_view scan_no_result;
    std::string_view apply_recommended;
    std::string_view scan_elevation;
    std::string_view scan_script_invalid;
    std::string_view scan_in_progress;
    std::string_view scan_invalid_timeout;
    std::string_view blacklist_title;
    std::string_view blacklist_hint;
    std::string_view save;
    std::string_view dns_title;
    std::string_view dns_v4_label;
    std::string_view dns_v6_label;
    std::string_view advanced_title;
    std::string_view advanced_hint;
    std::string_view apply;
};

[[nodiscard]] const Strings& english();

} // namespace sdp::gui
