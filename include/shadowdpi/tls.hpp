#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace sdp {

struct SniLocation {
    std::string_view hostname;
    std::size_t offset{0};
};

/// Returns true for payloads that start like a TLS ClientHello.
[[nodiscard]] bool is_tls_client_hello_prefix(std::span<const std::byte> payload) noexcept;

/// Extracts the SNI host name and its offset inside the payload.
/// The returned string_view points into the supplied payload.
[[nodiscard]] std::optional<SniLocation> extract_sni(
    std::span<const std::byte> payload);

} // namespace sdp
