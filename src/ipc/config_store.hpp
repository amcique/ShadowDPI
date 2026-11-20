#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sdp::ipc {

[[nodiscard]] std::filesystem::path default_config_path();
[[nodiscard]] std::filesystem::path default_blacklist_path();

[[nodiscard]] bool save_config(const std::filesystem::path& path,
                               const std::vector<std::string>& arguments,
                               std::string& error) noexcept;

[[nodiscard]] std::optional<std::vector<std::string>> load_config(
    const std::filesystem::path& path,
    std::string& error) noexcept;

} // namespace sdp::ipc
