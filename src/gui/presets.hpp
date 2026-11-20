#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sdp::gui {

struct Preset {
    std::string_view name;
    std::string_view description;
    std::vector<std::string> arguments;
};

[[nodiscard]] std::vector<Preset> presets();

} // namespace sdp::gui
