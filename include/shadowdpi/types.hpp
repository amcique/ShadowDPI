#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sdp {

inline constexpr std::size_t kMaxHostLength = 253;
// Matches WINDIVERT_MTU_MAX: a 40-byte IPv6 header plus a 16-bit payload.
inline constexpr std::size_t kMaxPacketSize = 65575;
inline constexpr std::size_t kMaxFakePackets = 30;
inline constexpr std::size_t kMaxFakePayloadSize = 1420;

inline constexpr std::uint16_t kHttpPort = 80;
inline constexpr std::uint16_t kHttpsPort = 443;

/// Payloads at or below this size are never treated as HTTP/TLS so that
/// TCP control chatter cannot trigger parser work.
inline constexpr std::size_t kMinTcpPayloadSize = 16;

enum class IpFamily {
    v4,
    v6,
};

struct IpAddress {
    IpFamily family{IpFamily::v4};
    std::array<std::uint8_t, 16> bytes{};

    [[nodiscard]] std::array<std::uint32_t, 4> words() const noexcept
    {
        std::array<std::uint32_t, 4> result{};
        const std::size_t size = family == IpFamily::v4 ? 4 : bytes.size();
        std::memcpy(result.data(), bytes.data(), size);
        return result;
    }

    friend bool operator==(const IpAddress&, const IpAddress&) = default;
};

} // namespace sdp
