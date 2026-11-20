#include <iostream>
#include <string>

#include "presets.hpp"
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

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "gui tests passed\n";
    return 0;
}
