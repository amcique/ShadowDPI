#include "scan.hpp"

#include <nlohmann/json.hpp>

namespace sdp::gui {

std::vector<std::string> build_scan_arguments(const ScanOptions& options,
                                              const std::string& json_path,
                                              const std::string& progress_path)
{
    std::vector<std::string> arguments{"-Action", "scan", "-Silent",
                                       "-ScanLevel", options.scan_level,
                                       "-Protocols", options.protocols,
                                       "-IpVersion", options.ip_version,
                                       "-TimeoutSec",
                                       std::to_string(options.timeout_sec),
                                       "-Json", json_path,
                                       "-ProgressFile", progress_path};
    if (!options.url_file.empty()) {
        arguments.push_back("-UrlFile");
        arguments.push_back(options.url_file);
    }
    if (!options.urls.empty()) {
        std::string joined;
        for (const auto& url : options.urls) {
            if (!joined.empty()) {
                joined += ',';
            }
            joined += url;
        }
        arguments.push_back("-Urls");
        arguments.push_back(joined);
    }
    if (!options.dns_addr.empty()) {
        arguments.push_back("-DnsAddr");
        arguments.push_back(options.dns_addr);
        arguments.push_back("-DnsPort");
        arguments.push_back(std::to_string(options.dns_port));
    }
    else {
        arguments.push_back("-DnsResolver");
        arguments.push_back("system");
    }
    return arguments;
}

std::optional<ProgressEntry> parse_progress_line(std::string_view line)
{
    try {
        const auto value = nlohmann::json::parse(line);
        if (!value.contains("index") || !value.contains("name")) {
            return std::nullopt;
        }
        ProgressEntry entry;
        entry.index = value.value("index", 0);
        entry.total = value.value("total", 0);
        entry.name = value.value("name", std::string{});
        entry.verdict = value.value("verdict", std::string{});
        entry.targets_passed = value.value("targetsPassed", 0);
        entry.target_total = value.value("targetTotal", 0);
        entry.regressions = value.value("regressions", 0);
        return entry;
    }
    catch (...) {
        return std::nullopt;
    }
}

std::optional<ScanSummary> parse_scan_summary(std::string_view text)
{
    try {
        const auto value = nlohmann::json::parse(text);
        ScanSummary summary;
        summary.generated_utc = value.value("generatedUtc", std::string{});
        summary.targets_blocked = value.value("targetsBlocked", 0);
        nlohmann::json recommended;
        if (value.contains("recommended")) {
            recommended = value["recommended"];
        }
        if (recommended.is_object()) {
            summary.has_recommendation = true;
            summary.recommendation.name =
                recommended.value("name", std::string{});
            summary.recommendation.targets_passed =
                recommended.value("targetsPassed", 0);
            summary.recommendation.target_total =
                recommended.value("targetTotal", 0);
            summary.recommendation.regressions =
                recommended.value("regressions", 0);
            for (const auto& argument :
                 recommended.value("args", nlohmann::json::array())) {
                if (argument.is_string()) {
                    summary.recommendation.args.push_back(
                        argument.get<std::string>());
                }
            }
        }
        return summary;
    }
    catch (...) {
        return std::nullopt;
    }
}

std::vector<ProgressEntry> ProgressCollector::consume(std::string_view text)
{
    std::vector<ProgressEntry> result;
    std::size_t ordinal = 0;
    std::size_t position = 0;
    while (position < text.size()) {
        const auto newline = text.find('\n', position);
        const auto length = newline == std::string_view::npos
                                ? text.size() - position
                                : newline - position;
        const auto line = text.substr(position, length);
        position += length + (newline == std::string_view::npos ? 0 : 1);

        if (line.empty()) {
            continue;
        }
        const bool already_seen = ordinal++ < seen_lines_;
        if (already_seen) {
            continue;
        }
        const auto entry = parse_progress_line(line);
        if (!entry) {
            // A line may be only partially written. Retry it next pass and
            // keep later lines pending so entries stay ordered and unique.
            break;
        }
        result.push_back(*entry);
        ++seen_lines_;
    }
    return result;
}

} // namespace sdp::gui
