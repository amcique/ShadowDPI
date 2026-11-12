#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "shadowdpi/config.hpp"

namespace sdp {

/// Builds the WinDivert filter for the main capture handle.
[[nodiscard]] std::string build_main_filter(const Config& config);

/// Builds the IPv4-only filter that drops spoofed RST packets.
[[nodiscard]] std::string build_passive_filter(const Config& config);

/// Builds the filter that drops QUIC Initial packets.
[[nodiscard]] std::string build_quic_filter();

} // namespace sdp
