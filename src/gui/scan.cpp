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
    for (const auto& url : options.urls) {
        arguments.push_back("-Urls");
        arguments.push_back(url);
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
                summary.recommendation.args.push_back(
                    argument.get<std::string>());
            }
        }
        return summary;
    }
    catch (...) {
        return std::nullopt;
    }
}

} // namespace sdp::gui
