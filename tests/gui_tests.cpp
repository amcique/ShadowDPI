#include <algorithm>
#include <iostream>
#include <string>

#include "presets.hpp"
#include "scan.hpp"
#include "scan_command.hpp"
#include "strings.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << __FILE__ << ':' << __LINE__                           \
                      << ": check failed: " #condition "\n";                   \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

} // namespace

int main()
{
    const auto presets = sdp::gui::presets();
    CHECK(presets.size() == 9);
    for (std::size_t index = 0; index < presets.size(); ++index) {
        const auto& preset = presets[index];
        CHECK(!preset.arguments.empty());
        CHECK(!preset.description.empty());
        CHECK(!preset.name.empty());
        CHECK(preset.name[0] == '-');
        CHECK(preset.name.size() == 2);
        CHECK(preset.name[1] >= '1' && preset.name[1] <= '9');
    }

    const auto& first = presets.front();
    CHECK(first.name == "-1");
    CHECK(first.arguments.size() == 10);
    CHECK(first.arguments[0] == "-p");
    CHECK(first.arguments.back() == "2");

    const auto& last = presets.back();
    CHECK(last.name == "-9");
    CHECK(last.arguments.size() == 1);
    CHECK(last.arguments[0] == "-9");

    const auto& strings = sdp::gui::english();
    CHECK(!strings.app_name.empty());
    CHECK(!strings.open_dashboard.empty());
    CHECK(!strings.emergency_stop.empty());
    CHECK(!strings.dashboard_title.empty());

    sdp::gui::ScanOptions options;
    options.urls.push_back("example.com");
    options.url_file = "C:\\urls.txt";
    options.scan_level = "quick";
    options.dns_addr = "1.1.1.1";
    const auto arguments = sdp::gui::build_scan_arguments(
        options, "C:\\temp\\result.json", "C:\\temp\\progress.jsonl");
    CHECK(arguments.size() > 10);
    CHECK(arguments[0] == "-Action");
    CHECK(arguments[1] == "scan");
    CHECK(std::find(arguments.begin(), arguments.end(), "-Json") !=
          arguments.end());
    CHECK(std::find(arguments.begin(), arguments.end(), "quick") !=
          arguments.end());
    CHECK(std::find(arguments.begin(), arguments.end(), "1.1.1.1") !=
          arguments.end());
    CHECK(std::count(arguments.begin(), arguments.end(), "-Urls") == 1);
    CHECK(std::find(arguments.begin(), arguments.end(), "example.com") !=
          arguments.end());
    options.dns_addr.clear();
    const auto system_dns = sdp::gui::build_scan_arguments(
        options, "C:\\temp\\result.json", "C:\\temp\\progress.jsonl");
    CHECK(std::find(system_dns.begin(), system_dns.end(), "-DnsResolver") !=
          system_dns.end());
    CHECK(std::find(system_dns.begin(), system_dns.end(), "system") !=
          system_dns.end());

    const auto progress = sdp::gui::parse_progress_line(
        "{\"index\":1,\"total\":9,\"name\":\"preset 2\","
        "\"verdict\":\"pass\",\"targetsPassed\":3,\"targetTotal\":3,"
        "\"regressions\":0}");
    CHECK(progress.has_value());
    if (progress) {
        CHECK(progress->index == 1);
        CHECK(progress->total == 9);
        CHECK(progress->verdict == "pass");
        CHECK(progress->targets_passed == 3);
    }
    CHECK(!sdp::gui::parse_progress_line("not json").has_value());

    const auto summary = sdp::gui::parse_scan_summary(
        "{\"generatedUtc\":\"2026-11-20T00:00:00Z\",\"targetsBlocked\":3,"
        "\"recommended\":{\"name\":\"preset 9\",\"args\":[\"-9\"],"
        "\"targetsPassed\":3,\"targetTotal\":3,\"regressions\":0}}");
    CHECK(summary.has_value());
    if (summary) {
        CHECK(summary->targets_blocked == 3);
        CHECK(summary->has_recommendation);
        CHECK(summary->recommendation.name == "preset 9");
        CHECK(summary->recommendation.args.size() == 1);
        CHECK(summary->recommendation.args[0] == "-9");
    }
    CHECK(sdp::gui::parse_scan_summary("{}").has_value());
    CHECK(!sdp::gui::parse_scan_summary("broken").has_value());

    const auto mixed_summary = sdp::gui::parse_scan_summary(
        "{\"recommended\":{\"name\":\"preset 9\",\"args\":[\"-9\",7]}}");
    CHECK(mixed_summary.has_value());
    if (mixed_summary) {
        CHECK(mixed_summary->recommendation.args.size() == 1);
    }

    sdp::gui::ProgressCollector collector;
    CHECK(collector.consume(
              "{\"index\":0,\"name\":\"preset 1\",\"verdict\":\"pass\"}\n")
              .size() == 1);
    CHECK(collector.consume(
              "{\"index\":0,\"name\":\"preset 1\",\"verdict\":\"pass\"}\n")
              .empty());
    sdp::gui::ProgressCollector partial_collector;
    CHECK(partial_collector.consume(
              "{\"index\":0,\"name\":\"preset 1\",\"verdict\":\"pass\"}\n"
              "partial\n"
              "{\"index\":1,\"name\":\"preset 2\",\"verdict\":\"fail\"}\n")
              .size() == 1);
    CHECK(partial_collector.consume(
              "{\"index\":0,\"name\":\"preset 1\",\"verdict\":\"pass\"}\n"
              "{\"index\":1,\"name\":\"preset 2\",\"verdict\":\"fail\"}\n")
              .size() == 1);
    CHECK(partial_collector.consume(
              "{\"index\":0,\"name\":\"preset 1\",\"verdict\":\"pass\"}\n"
              "{\"index\":1,\"name\":\"preset 2\",\"verdict\":\"fail\"}\n")
              .empty());
    const auto pending = collector.consume(
        "{\"index\":0,\"name\":\"preset 1\",\"verdict\":\"pass\"}\n"
        "{\"index\":1,\"name\":\"preset 2\",\"verdict\":\"fail\"}\n");
    CHECK(pending.size() >= 1);
    CHECK(collector.consume(
              "{\"index\":0,\"name\":\"preset 1\",\"verdict\":\"pass\"}\n"
              "{\"index\":1,\"name\":\"preset 2\",\"verdict\":\"fail\"}\n")
              .empty());

    const auto command = sdp::gui::build_elevated_command(
        L"abc123", L"C:\\Tools\\ShadowDPI\\install.ps1",
        {L"-Action", L"scan", L"-Urls", L"example.com,b.com", L"-Silent"});
    CHECK(command.find(L"-Action 'scan'") != std::wstring::npos);
    CHECK(command.find(L"'-Action'") == std::wstring::npos);
    CHECK(command.find(L"-Urls 'example.com','b.com'") !=
          std::wstring::npos);
    CHECK(command.find(L"'abc123'") != std::wstring::npos);
    CHECK(command.find(L"[ScriptBlock]::Create($t)") != std::wstring::npos);

    // A value that begins with '-' must stay quoted data. A switch-looking
    // value must never become a bare token in the elevated command.
    const auto hostile = sdp::gui::build_elevated_command(
        L"abc123", L"C:\\Tools\\ShadowDPI\\install.ps1",
        {L"-Action", L"scan", L"-Silent", L"-UrlFile",
         L"-x;Start-Process calc.exe;", L"-DnsAddr", L"-9"});
    CHECK(hostile.find(L"-UrlFile '-x;Start-Process calc.exe;'") !=
          std::wstring::npos);
    CHECK(hostile.find(L"-DnsAddr '-9'") != std::wstring::npos);
    CHECK(hostile.find(L" -x;") == std::wstring::npos);
    CHECK(hostile.find(L" -9") == std::wstring::npos);
    CHECK(hostile.find(L"'-Silent'") == std::wstring::npos);

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "gui tests passed\n";
    return 0;
}
