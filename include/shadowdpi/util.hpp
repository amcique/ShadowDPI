#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "shadowdpi/types.hpp"

namespace sdp {

[[nodiscard]] std::string replace_all(std::string input,
                                      std::string_view from,
                                      std::string_view to);

[[nodiscard]] std::uint16_t read_be16(const std::byte* data) noexcept;
[[nodiscard]] std::uint32_t read_be32(const std::byte* data) noexcept;
void write_be16(std::byte* data, std::uint16_t value) noexcept;

[[nodiscard]] std::string to_ascii_lower(std::string_view value);
[[nodiscard]] bool ascii_iequals(std::string_view lhs, std::string_view rhs) noexcept;

[[nodiscard]] std::optional<IpAddress> parse_ip_address(std::string_view text);
[[nodiscard]] std::string to_string(const IpAddress& address);

/// Parses a sequence of hexadecimal octets (for example "160301", with no spaces).
/// Returns std::nullopt for empty, odd-length, or otherwise invalid input.
[[nodiscard]] std::optional<std::vector<std::byte>> parse_hex_payload(std::string_view text);

} // namespace sdp
