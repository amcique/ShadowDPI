#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sdp {

/// Built-in fake HTTP request (curl-like).
[[nodiscard]] std::span<const std::byte> default_http_fake() noexcept;

/// Built-in fake TLS ClientHello for www.w3.org.
[[nodiscard]] std::span<const std::byte> default_https_fake() noexcept;

/// Generates a Firefox-like TLS ClientHello with the requested SNI. The domain
/// must be 1..253 bytes; an empty vector is returned otherwise.
[[nodiscard]] std::vector<std::byte> make_fake_client_hello(std::string_view domain);

/// Generates random fake payloads of 8..(max_size-1) bytes. Requires
/// max_size >= 9, count > 0 and count <= kMaxFakePackets; an empty vector is
/// returned otherwise.
[[nodiscard]] std::vector<std::vector<std::byte>> make_random_fake_payloads(
    unsigned int count,
    unsigned int max_size);

} // namespace sdp
