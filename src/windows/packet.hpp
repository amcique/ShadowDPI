#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "shadowdpi/types.hpp"
#include "win_headers.hpp"

namespace sdp::win {

struct PacketBuffer {
    std::array<std::byte, kMaxPacketSize> data{};
    UINT length{0};
    WINDIVERT_ADDRESS address{};
};

struct ParsedPacket {
    PWINDIVERT_IPHDR ipv4{nullptr};
    PWINDIVERT_IPV6HDR ipv6{nullptr};
    PWINDIVERT_TCPHDR tcp{nullptr};
    PWINDIVERT_UDPHDR udp{nullptr};
    std::byte* payload{nullptr};
    UINT payload_length{0};

    [[nodiscard]] bool is_ipv4() const noexcept { return ipv4 != nullptr; }
    [[nodiscard]] bool is_ipv6() const noexcept { return ipv6 != nullptr; }
    [[nodiscard]] bool has_tcp() const noexcept { return tcp != nullptr; }
    [[nodiscard]] bool has_udp() const noexcept { return udp != nullptr; }

    [[nodiscard]] bool has_tcp_data() const noexcept
    {
        return tcp != nullptr && payload != nullptr && payload_length > 0;
    }

    [[nodiscard]] bool has_udp_data() const noexcept
    {
        return udp != nullptr && payload != nullptr && payload_length > 0;
    }

    [[nodiscard]] std::span<std::byte> payload_span() const noexcept
    {
        if (payload == nullptr || payload_length == 0) {
            return {};
        }
        return {payload, payload_length};
    }
};

/// Copies only the valid bytes and metadata of `source`.
///
/// Assigning a PacketBuffer copies the whole fixed-size buffer; packet code
/// usually only needs the bytes WinDivert actually wrote.
void copy_packet(PacketBuffer& destination,
                 const PacketBuffer& source) noexcept;

/// Parses headers inside a raw WinDivert packet buffer.
[[nodiscard]] ParsedPacket parse_packet(std::byte* data, UINT length) noexcept;

/// Re-points a parse result at a byte-for-byte copy of the same packet.
///
/// `old_base` must be the buffer `parsed` was produced from and `new_base` a
/// copy with an identical layout. Pointers inside the packet move by the same
/// offset; null pointers stay null. This avoids parsing scratch-buffer copies
/// of packets whose layout is already known.
[[nodiscard]] ParsedPacket rebase_packet(const ParsedPacket& parsed,
                                         const std::byte* old_base,
                                         std::byte* new_base) noexcept;

/// Recomputes checksums after the packet was modified in place.
void recalculate_checksums(std::byte* data,
                           UINT length,
                           WINDIVERT_ADDRESS& address) noexcept;

/// Updates the IPv4 total length or the IPv6 payload length field.
void set_ip_length(ParsedPacket& packet, UINT length) noexcept;

[[nodiscard]] std::array<std::uint32_t, 4> ipv4_words(
    std::uint32_t address) noexcept;
[[nodiscard]] std::array<std::uint32_t, 4> ipv6_words(
    const std::uint32_t* address) noexcept;

void store_ipv4(std::uint32_t& target,
                const std::array<std::uint32_t, 4>& words) noexcept;
void store_ipv6(std::uint32_t* target,
                const std::array<std::uint32_t, 4>& words) noexcept;

[[nodiscard]] std::array<std::uint32_t, 4> source_address(
    const ParsedPacket& packet) noexcept;
[[nodiscard]] std::array<std::uint32_t, 4> destination_address(
    const ParsedPacket& packet) noexcept;

} // namespace sdp::win
