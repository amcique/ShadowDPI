#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sdp::gui {

/// Renders the PowerShell -Command string used for an elevated scan. Parameter
/// names stay bare so PowerShell binds them by name; only values are quoted.
[[nodiscard]] std::wstring build_elevated_command(
    std::wstring_view expected_hash,
    std::wstring_view script_path,
    const std::vector<std::wstring>& arguments);

} // namespace sdp::gui
