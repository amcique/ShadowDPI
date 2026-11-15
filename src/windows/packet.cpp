#include "packet.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "windivert_api.hpp"

namespace sdp::win {
namespace {

constexpr UINT kIpv4HeaderLength = 20;
constexpr UINT kIpv6HeaderLength = 40;
constexpr UINT kTcpMinimumHeaderLength = 20;
constexpr UINT kUdpHeaderLength = 8;

/// Fragment offset in 8-byte units plus the more-fragments flag. Both mean the
/// packet does not carry a complete transport segment.
constexpr UINT16 kIpv4FragmentMask = 0x3FFFU;

[[nodiscard]] inline std::uint8_t byte_value(std::byte value) noexcept
{
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] inline bool try_parse_tcp(std::byte* data,
                                        UINT length,
                                        UINT offset,
                                        ParsedPacket& parsed) noexcept
{
    if (length < offset + kTcpMinimumHeaderLength) {
        return false;
    }

    auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(data + offset);
    const UINT header_length = static_cast<UINT>(tcp->HdrLength) * 4U;
    if (header_length < kTcpMinimumHeaderLength ||
        offset + header_length > length) {
        return false;
    }

    parsed.tcp = tcp;
    const UINT payload_length = length - offset - header_length;
    if (payload_length > 0) {
        parsed.payload = data + offset + header_length;
        parsed.payload_length = payload_length;
    }
    return true;
}

[[nodiscard]] inline bool try_parse_udp(std::byte* data,
                                        UINT length,
                                        UINT offset,
                                        ParsedPacket& parsed) noexcept
{
    if (length < offset + kUdpHeaderLength) {
        return false;
    }

    auto* udp = reinterpret_cast<PWINDIVERT_UDPHDR>(data + offset);
    const UINT udp_length = ntohs(udp->Length);
    if (udp_length < kUdpHeaderLength || offset + udp_length != length) {
        return false;
    }

    parsed.udp = udp;
    if (udp_length > kUdpHeaderLength) {
        parsed.payload = data + offset + kUdpHeaderLength;
        parsed.payload_length = udp_length - kUdpHeaderLength;
    }
    return true;
}

/// Fast path for the packets that make up virtually all real traffic:
/// unfragmented IPv4 with a 20-byte header, or IPv6 without extension
/// headers, carrying TCP or UDP. Any unusual packet shape falls back to
/// WinDivert's parser, which keeps parity with the original behavior.
[[nodiscard]] inline bool try_parse_fast(std::byte* data,
                                        UINT length,
                                        ParsedPacket& parsed) noexcept
{
    if (length < kIpv4HeaderLength) {
        return false;
    }

    // Version and IPv4 header length share the first byte; load it once.
    const std::uint8_t first = byte_value(data[0]);
    const std::uint8_t version = static_cast<std::uint8_t>(first >> 4U);

    if (version == 4) {
        // Require an unfragmented packet whose recorded length covers exactly
        // the bytes WinDivert handed us. Anything else (IP options, trailing
        // bytes, fragments) goes through the helper so behavior cannot drift.
        auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(data);
        if ((first & 0x0FU) != 5 ||
            ip->Length != htons(static_cast<UINT16>(length)) ||
            (ip->FragOff0 & htons(kIpv4FragmentMask)) != 0) {
            return false;
        }

        parsed.ipv4 = ip;
        if (ip->Protocol == IPPROTO_TCP) {
            if (try_parse_tcp(data, length, kIpv4HeaderLength, parsed)) {
                return true;
            }
        }
        else if (ip->Protocol == IPPROTO_UDP) {
            if (try_parse_udp(data, length, kIpv4HeaderLength, parsed)) {
                return true;
            }
        }
        parsed = {};
        return false;
    }

    if (version == 6) {
        auto* ip = reinterpret_cast<PWINDIVERT_IPV6HDR>(data);
        if (length < kIpv6HeaderLength ||
            length - kIpv6HeaderLength != ntohs(ip->Length)) {
            return false;
        }

        parsed.ipv6 = ip;
        if (ip->NextHdr == IPPROTO_TCP) {
            if (try_parse_tcp(data, length, kIpv6HeaderLength, parsed)) {
                return true;
            }
        }
        else if (ip->NextHdr == IPPROTO_UDP) {
            if (try_parse_udp(data, length, kIpv6HeaderLength, parsed)) {
                return true;
            }
        }
        parsed = {};
        return false;
    }

    return false;
}

template <typename T>
void rebase_pointer(T*& pointer,
                    const std::byte* old_base,
                    std::byte* new_base) noexcept
{
    if (pointer == nullptr) {
        return;
    }

    const auto offset =
        reinterpret_cast<const std::byte*>(pointer) - old_base;
    pointer = reinterpret_cast<T*>(new_base + offset);
}

[[nodiscard]] ParsedPacket parse_packet_with_helper(std::byte* data,
                                                    UINT length) noexcept
{
    const auto& api = windivert_api();
    if (api.helper_parse == nullptr) {
        return ParsedPacket{};
    }

    ParsedPacket parsed;
    PVOID payload = nullptr;
    UINT payload_length = 0;
    if (!api.helper_parse(data, length, &parsed.ipv4, &parsed.ipv6, nullptr,
                          nullptr, nullptr, &parsed.tcp, &parsed.udp, &payload,
                          &payload_length, nullptr, nullptr)) {
        return ParsedPacket{};
    }

    parsed.payload = static_cast<std::byte*>(payload);
    parsed.payload_length = payload_length;
    return parsed;
}

} // namespace

void copy_packet(PacketBuffer& destination,
                 const PacketBuffer& source) noexcept
{
    const auto bytes = std::min<std::size_t>(source.length,
                                             destination.data.size());
    destination.length = static_cast<UINT>(bytes);
    destination.address = source.address;
    std::memcpy(destination.data.data(), source.data.data(), bytes);
}

ParsedPacket parse_packet(std::byte* data, UINT length) noexcept
{
    if (data == nullptr || length == 0) {
        return {};
    }

    ParsedPacket parsed;
    if (try_parse_fast(data, length, parsed)) {
        return parsed;
    }
    return parse_packet_with_helper(data, length);
}

ParsedPacket rebase_packet(const ParsedPacket& parsed,
                           const std::byte* old_base,
                           std::byte* new_base) noexcept
{
    ParsedPacket result = parsed;
    rebase_pointer(result.ipv4, old_base, new_base);
    rebase_pointer(result.ipv6, old_base, new_base);
    rebase_pointer(result.tcp, old_base, new_base);
    rebase_pointer(result.udp, old_base, new_base);
    rebase_pointer(result.payload, old_base, new_base);
    return result;
}

void recalculate_checksums(std::byte* data,
                           UINT length,
                           WINDIVERT_ADDRESS& address) noexcept
{
    const auto& api = windivert_api();
    if (api.helper_calc_checksums != nullptr) {
        api.helper_calc_checksums(data, length, &address, 0);
    }
}

void set_ip_length(ParsedPacket& packet, UINT length) noexcept
{
    const auto total = static_cast<UINT16>(length);
    if (packet.ipv4 != nullptr) {
        packet.ipv4->Length = htons(total);
    }
    else if (packet.ipv6 != nullptr) {
        // IPv6 Length excludes the fixed 40-byte IPv6 header.
        packet.ipv6->Length =
            htons(static_cast<UINT16>(length - kIpv6HeaderLength));
    }
}

std::array<std::uint32_t, 4> ipv4_words(std::uint32_t address) noexcept
{
    return {address, 0, 0, 0};
}

std::array<std::uint32_t, 4> ipv6_words(const std::uint32_t* address) noexcept
{
    std::array<std::uint32_t, 4> words{};
    if (address != nullptr) {
        std::memcpy(words.data(), address, sizeof(words));
    }
    return words;
}

void store_ipv4(std::uint32_t& target,
                const std::array<std::uint32_t, 4>& words) noexcept
{
    target = words[0];
}

void store_ipv6(std::uint32_t* target,
                const std::array<std::uint32_t, 4>& words) noexcept
{
    if (target != nullptr) {
        std::memcpy(target, words.data(), sizeof(words));
    }
}

std::array<std::uint32_t, 4> source_address(
    const ParsedPacket& packet) noexcept
{
    if (packet.ipv4 != nullptr) {
        return ipv4_words(packet.ipv4->SrcAddr);
    }
    if (packet.ipv6 != nullptr) {
        return ipv6_words(packet.ipv6->SrcAddr);
    }
    return {};
}

std::array<std::uint32_t, 4> destination_address(
    const ParsedPacket& packet) noexcept
{
    if (packet.ipv4 != nullptr) {
        return ipv4_words(packet.ipv4->DstAddr);
    }
    if (packet.ipv6 != nullptr) {
        return ipv6_words(packet.ipv6->DstAddr);
    }
    return {};
}

} // namespace sdp::win
