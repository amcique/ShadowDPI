#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace sdp {

struct HeaderLocation {
    std::size_t name_offset{0};
    std::size_t value_offset{0};
    std::size_t value_length{0};
};

/// Finds an HTTP header such as "\r\nHost: " in a payload.
[[nodiscard]] std::optional<HeaderLocation> find_header(
    std::span<const std::byte> payload,
    std::string_view header_name);

/// Detects the redirect page used by passive DPI systems.
[[nodiscard]] bool is_passive_dpi_redirect(std::span<const std::byte> payload);

/// Returns the offset of the trailing space after an HTTP method.
[[nodiscard]] std::optional<std::size_t> find_http_method_end(
    std::span<const std::byte> payload,
    unsigned int fragment_size,
    bool* fragmented = nullptr);

/// Converts every odd-indexed byte of a host value to upper case.
void mix_case(std::span<std::byte> value);

/// Replaces "\r\nHost: " with "\r\nhoSt: ".
void replace_host_name(std::span<std::byte> payload, const HeaderLocation& host);

/// Moves the space between "Host:" and its value into the User-Agent value.
/// Returns false when the layout cannot be adjusted safely.
[[nodiscard]] bool move_host_space_to_user_agent(
    std::span<std::byte> payload,
    const HeaderLocation& host,
    const HeaderLocation& user_agent);

/// Duplicates the space between the method and the request URI.
[[nodiscard]] bool add_extra_space_before_uri(
    std::span<std::byte> payload,
    std::size_t method_end,
    std::size_t host_value_offset);

} // namespace sdp
