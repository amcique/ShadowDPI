#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdp::gui {

struct ScanOptions {
    std::vector<std::string> urls;
    std::string url_file;
    std::string scan_level{"standard"};
    std::string protocols{"auto"};
    std::string ip_version{"4"};
    int timeout_sec{5};
    std::string dns_addr;
    int dns_port{53};
};

struct ProgressEntry {
    int index{0};
    int total{0};
    std::string name;
    std::string verdict;
    int targets_passed{0};
    int target_total{0};
    int regressions{0};
};

struct ScanRecommendation {
    std::string name;
    std::vector<std::string> args;
    int targets_passed{0};
    int target_total{0};
    int regressions{0};
};

struct ScanSummary {
    std::string generated_utc;
    int targets_blocked{0};
    bool has_recommendation{false};
    ScanRecommendation recommendation;
};

[[nodiscard]] std::vector<std::string> build_scan_arguments(
    const ScanOptions& options,
    const std::string& json_path,
    const std::string& progress_path);

[[nodiscard]] std::optional<ProgressEntry> parse_progress_line(
    std::string_view line);

[[nodiscard]] std::optional<ScanSummary> parse_scan_summary(
    std::string_view text);

/// Tracks which progress lines have already been consumed. Malformed or
/// partially written lines are retried on the next pass.
class ProgressCollector {
public:
    [[nodiscard]] std::vector<ProgressEntry> consume(std::string_view text);

private:
    std::size_t seen_lines_{0};
};

} // namespace sdp::gui
