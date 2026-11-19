#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "shadowdpi/cli.hpp"
#include "shadowdpi/config.hpp"
#include "shadowdpi/fake_payloads.hpp"
#include "shadowdpi/filter.hpp"
#include "shadowdpi/log.hpp"
#include "shadowdpi/types.hpp"
#include "shadowdpi/util.hpp"
#include "fake_sender.hpp"
#include "packet.hpp"
#include "packet_engine.hpp"
#include "win_headers.hpp"
#include "windivert_api.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << __FILE__ << ':' << __LINE__                          \
                      << ": check failed: " #condition "\n";                   \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

bool compile_filter(const std::string& filter, std::string& error)
{
    std::vector<char> object(1U << 20U);
    const char* error_string = nullptr;
    UINT error_position = 0;
    if (WinDivertHelperCompileFilter(
            filter.c_str(), WINDIVERT_LAYER_NETWORK, object.data(),
            static_cast<UINT>(object.size()), &error_string,
            &error_position)) {
        return true;
    }

    error = error_string != nullptr ? error_string : "unknown filter error";
    if (error_position < filter.size()) {
        error += " at offset " + std::to_string(error_position) + ": " +
                 filter.substr(error_position, 40);
    }
    return false;
}

void check_filter_compiles(const sdp::Config& config, const char* name)
{
    std::string error;

    const auto main_filter = sdp::build_main_filter(config);
    if (!compile_filter(main_filter, error)) {
        std::cerr << name << " main filter: " << error << '\n';
        ++failures;
    }

    const auto passive_filter = sdp::build_passive_filter(config);
    if (!compile_filter(passive_filter, error)) {
        std::cerr << name << " passive filter: " << error << '\n';
        ++failures;
    }
}

void test_filters_compile()
{
    sdp::Config config;
    check_filter_compiles(config, "defaults");

    config.extra_ports = {8080, 8443};
    check_filter_compiles(config, "extra ports");

    config.max_payload = 1200;
    config.ip_ids = {5, 6};
    check_filter_compiles(config, "payload and IP IDs");

    config.dns.v4_server = sdp::parse_ip_address("8.8.8.8");
    config.dns.v6_server = sdp::parse_ip_address("2001:4860:4860::8888");
    config.dns.v4_port = 5353;
    config.dns.v6_port = 5354;
    check_filter_compiles(config, "DNS redirect");

    const auto main_filter = sdp::build_main_filter(config);
    CHECK(main_filter.find("tcp.PayloadLength > 16") != std::string::npos);
    CHECK(main_filter.find("tcp.PayloadLength > 0") != std::string::npos);
    CHECK(main_filter.find("#MINPAYLOAD#") == std::string::npos);
    CHECK(main_filter.find("#TLSHANDSHAKEPREFIX#") == std::string::npos);
    CHECK(main_filter.find("tcp.Payload[0] == 0x16") != std::string::npos);
    CHECK(main_filter.find("udp.SrcPort == 5353") != std::string::npos);
    CHECK(main_filter.find("udp.SrcPort == 5354") != std::string::npos);

    const auto passive_filter = sdp::build_passive_filter(config);
    CHECK(passive_filter.find("(true") == std::string::npos);
    CHECK(passive_filter.find("ip.Id >= 0x0") != std::string::npos);
    CHECK(passive_filter.find("ip.Id == 5") != std::string::npos);

    const auto extra_ports = sdp::build_main_filter(config);
    CHECK(extra_ports.find("tcp.Ack") != std::string::npos);
    CHECK(extra_ports.find("tcp.PayloadLength > 0") != std::string::npos);

    const auto quic_filter = sdp::build_quic_filter();
    std::string error;
    CHECK(compile_filter(quic_filter, error));
    CHECK(quic_filter.find("0x6b3343cf") != std::string::npos);
}

using Buffer = std::array<std::byte, 256>;

struct RecordedPacket {
    std::vector<std::byte> data;
    UINT length{0};
    WINDIVERT_ADDRESS address{};
};

/// Captures every packet the engine generates instead of sending it.
class RecordingSink final : public sdp::win::PacketSink {
public:
    bool send(const sdp::win::PacketBuffer& packet) override
    {
        if (fail_sends) {
            return false;
        }
        RecordedPacket recorded;
        recorded.data.assign(packet.data.begin(),
                             packet.data.begin() + packet.length);
        recorded.length = packet.length;
        recorded.address = packet.address;
        packets.push_back(std::move(recorded));
        return true;
    }

    bool fail_sends{false};
    std::vector<RecordedPacket> packets;
};

template <std::size_t N>
void make_ipv4_tcp(std::array<std::byte, N>& buffer,
                   UINT payload_size,
                   UINT total_length,
                   std::uint8_t ip_header_words = 5,
                   std::uint8_t tcp_header_words = 5)
{
    buffer.fill(std::byte{0});
    auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(buffer.data());
    ip->Version = 4;
    ip->HdrLength = ip_header_words;
    ip->Length = htons(static_cast<UINT16>(total_length));
    ip->TTL = 64;
    ip->Protocol = IPPROTO_TCP;
    auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(
        buffer.data() + ip_header_words * 4U);
    tcp->HdrLength = tcp_header_words;
    tcp->SrcPort = htons(12345);
    tcp->DstPort = htons(443);
    tcp->Ack = 1;
    std::memset(buffer.data() + ip_header_words * 4U +
                    tcp_header_words * 4U,
                0x16, payload_size);
}

template <std::size_t N>
void make_ipv4_udp(std::array<std::byte, N>& buffer,
                   UINT payload_size,
                   UINT total_length)
{
    buffer.fill(std::byte{0});
    auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(buffer.data());
    ip->Version = 4;
    ip->HdrLength = 5;
    ip->Length = htons(static_cast<UINT16>(total_length));
    ip->TTL = 64;
    ip->Protocol = IPPROTO_UDP;
    auto* udp = reinterpret_cast<PWINDIVERT_UDPHDR>(buffer.data() + 20);
    udp->Length = htons(static_cast<UINT16>(8 + payload_size));
    std::memset(buffer.data() + 28, 0, payload_size);
}

void make_ipv6_tcp(Buffer& buffer, UINT payload_size)
{
    buffer.fill(std::byte{0});
    auto* ip = reinterpret_cast<PWINDIVERT_IPV6HDR>(buffer.data());
    ip->Version = 6;
    ip->Length = htons(static_cast<UINT16>(20 + payload_size));
    ip->NextHdr = IPPROTO_TCP;
    ip->HopLimit = 64;
    auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(buffer.data() + 40);
    tcp->HdrLength = 5;
    tcp->SrcPort = htons(12345);
    tcp->DstPort = htons(443);
    tcp->Ack = 1;
    std::memset(buffer.data() + 60, 0x16, payload_size);
}

void make_ipv6_udp(Buffer& buffer, UINT payload_size)
{
    buffer.fill(std::byte{0});
    auto* ip = reinterpret_cast<PWINDIVERT_IPV6HDR>(buffer.data());
    ip->Version = 6;
    ip->Length = htons(static_cast<UINT16>(8 + payload_size));
    ip->NextHdr = IPPROTO_UDP;
    ip->HopLimit = 64;
    auto* udp = reinterpret_cast<PWINDIVERT_UDPHDR>(buffer.data() + 40);
    udp->Length = htons(static_cast<UINT16>(8 + payload_size));
    udp->SrcPort = htons(53);
    udp->DstPort = htons(12345);
    std::memset(buffer.data() + 48, 0, payload_size);
}

// IPv6 with a single 8-byte hop-by-hop extension header before TCP.
void make_ipv6_tcp_hopopts(Buffer& buffer, UINT payload_size)
{
    buffer.fill(std::byte{0});
    auto* ip = reinterpret_cast<PWINDIVERT_IPV6HDR>(buffer.data());
    ip->Version = 6;
    ip->Length = htons(static_cast<UINT16>(8 + 20 + payload_size));
    ip->NextHdr = 0; // Hop-by-hop options
    ip->HopLimit = 64;

    auto* extension = buffer.data() + 40;
    extension[0] = std::byte{IPPROTO_TCP};
    extension[1] = std::byte{0}; // 8-byte header

    auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(buffer.data() + 48);
    tcp->HdrLength = 5;
    tcp->SrcPort = htons(12345);
    tcp->DstPort = htons(443);
    tcp->Ack = 1;
    std::memset(buffer.data() + 68, 0x16, payload_size);
}

void make_ipv4_icmp(Buffer& buffer)
{
    buffer.fill(std::byte{0});
    auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(buffer.data());
    ip->Version = 4;
    ip->HdrLength = 5;
    ip->Length = htons(28);
    ip->TTL = 64;
    ip->Protocol = IPPROTO_ICMP;
    buffer[20] = std::byte{8}; // Echo request
}

void make_ipv6_icmp(Buffer& buffer)
{
    buffer.fill(std::byte{0});
    auto* ip = reinterpret_cast<PWINDIVERT_IPV6HDR>(buffer.data());
    ip->Version = 6;
    ip->Length = htons(8);
    ip->NextHdr = IPPROTO_ICMPV6;
    ip->HopLimit = 64;
    buffer[40] = std::byte{128}; // Echo request
}

std::uint32_t ipv4_address(std::uint8_t a,
                           std::uint8_t b,
                           std::uint8_t c,
                           std::uint8_t d)
{
    return htonl((static_cast<std::uint32_t>(a) << 24U) |
                 (static_cast<std::uint32_t>(b) << 16U) |
                 (static_cast<std::uint32_t>(c) << 8U) |
                 static_cast<std::uint32_t>(d));
}

std::vector<std::byte> byte_string(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

void make_tcp_payload_packet(sdp::win::PacketBuffer& packet,
                             bool outbound,
                             std::uint16_t source_port,
                             std::uint16_t destination_port,
                             std::span<const std::byte> payload)
{
    const UINT payload_size = static_cast<UINT>(payload.size());
    make_ipv4_tcp(packet.data, payload_size, 40 + payload_size);
    packet.length = 40 + payload_size;
    packet.address = {};
    packet.address.Outbound = outbound ? 1 : 0;

    auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(packet.data.data());
    ip->SrcAddr = outbound ? ipv4_address(192, 168, 1, 2)
                           : ipv4_address(8, 8, 8, 8);
    ip->DstAddr = outbound ? ipv4_address(8, 8, 8, 8)
                           : ipv4_address(192, 168, 1, 2);

    auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(packet.data.data() + 20);
    tcp->SrcPort = htons(source_port);
    tcp->DstPort = htons(destination_port);
    tcp->SeqNum = htonl(1000);
    tcp->Ack = 1;
    std::memcpy(packet.data.data() + 40, payload.data(), payload_size);
}

void make_tcp_control_packet(sdp::win::PacketBuffer& packet,
                             bool outbound,
                             std::uint16_t source_port,
                             std::uint16_t destination_port,
                             bool syn,
                             bool ack)
{
    make_ipv4_tcp(packet.data, 0, 40);
    packet.length = 40;
    packet.address = {};
    packet.address.Outbound = outbound ? 1 : 0;

    auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(packet.data.data());
    ip->SrcAddr = outbound ? ipv4_address(192, 168, 1, 2)
                           : ipv4_address(8, 8, 8, 8);
    ip->DstAddr = outbound ? ipv4_address(8, 8, 8, 8)
                           : ipv4_address(192, 168, 1, 2);

    auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(packet.data.data() + 20);
    tcp->SrcPort = htons(source_port);
    tcp->DstPort = htons(destination_port);
    tcp->Syn = syn ? 1 : 0;
    tcp->Ack = ack ? 1 : 0;
    tcp->Window = htons(65535);
}

void make_udp_payload_packet(sdp::win::PacketBuffer& packet,
                             bool outbound,
                             std::uint32_t source_ip,
                             std::uint32_t destination_ip,
                             std::uint16_t source_port,
                             std::uint16_t destination_port,
                             std::span<const std::byte> payload)
{
    const UINT payload_size = static_cast<UINT>(payload.size());
    make_ipv4_udp(packet.data, payload_size, 28 + payload_size);
    packet.length = 28 + payload_size;
    packet.address = {};
    packet.address.Outbound = outbound ? 1 : 0;

    auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(packet.data.data());
    ip->SrcAddr = source_ip;
    ip->DstAddr = destination_ip;

    auto* udp = reinterpret_cast<PWINDIVERT_UDPHDR>(packet.data.data() + 20);
    udp->SrcPort = htons(source_port);
    udp->DstPort = htons(destination_port);
    std::memcpy(packet.data.data() + 28, payload.data(), payload_size);
}

std::array<std::byte, 29> make_dns_payload(bool response)
{
    std::array<std::byte, 29> payload{};
    payload[0] = std::byte{0x12}; // transaction ID
    payload[1] = std::byte{0x34};
    payload[2] = response ? std::byte{0x81} : std::byte{0x01};
    payload[3] = response ? std::byte{0x80} : std::byte{0x00};
    payload[4] = std::byte{0x00};
    payload[5] = std::byte{0x01}; // QDCOUNT

    constexpr std::string_view example = "example";
    constexpr std::string_view com = "com";
    payload[12] = std::byte{static_cast<unsigned char>(example.size())};
    for (std::size_t index = 0; index < example.size(); ++index) {
        payload[13 + index] = static_cast<std::byte>(
            static_cast<unsigned char>(example[index]));
    }

    const std::size_t com_offset = 13 + example.size();
    payload[com_offset] =
        std::byte{static_cast<unsigned char>(com.size())};
    for (std::size_t index = 0; index < com.size(); ++index) {
        payload[com_offset + 1 + index] = static_cast<std::byte>(
            static_cast<unsigned char>(com[index]));
    }
    payload[com_offset + 1 + com.size()] = std::byte{0x00};

    const std::size_t type_offset = com_offset + 2 + com.size();
    payload[type_offset + 1] = std::byte{0x01};     // QTYPE A
    payload[type_offset + 3] = std::byte{0x01};     // QCLASS IN
    return payload;
}

std::array<std::byte, 29> dns_query_payload()
{
    return make_dns_payload(false);
}

std::array<std::byte, 29> dns_response_payload()
{
    return make_dns_payload(true);
}

sdp::win::ParsedPacket parse_packet_buffer(sdp::win::PacketBuffer& packet)
{
    return sdp::win::parse_packet(packet.data.data(), packet.length);
}

sdp::win::ParsedPacket parse_recorded(RecordedPacket& packet)
{
    return sdp::win::parse_packet(packet.data.data(), packet.length);
}

std::string payload_string(sdp::win::PacketBuffer& packet)
{
    const auto parsed = parse_packet_buffer(packet);
    if (parsed.payload == nullptr || parsed.payload_length == 0) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(parsed.payload),
                       parsed.payload_length);
}

template <std::size_t N>
void make_ipv4_tcp_packet(std::array<std::byte, N>& buffer,
                          bool outbound,
                          std::uint16_t source_port,
                          std::uint16_t destination_port,
                          UINT payload_size,
                          bool syn,
                          bool ack,
                          std::uint16_t ip_id)
{
    make_ipv4_tcp(buffer, payload_size, 40 + payload_size);
    auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(buffer.data());
    ip->Id = htons(ip_id);
    ip->SrcAddr = outbound ? ipv4_address(192, 168, 1, 2)
                           : ipv4_address(8, 8, 8, 8);
    ip->DstAddr = outbound ? ipv4_address(8, 8, 8, 8)
                           : ipv4_address(192, 168, 1, 2);
    auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(buffer.data() + 20);
    tcp->SrcPort = htons(source_port);
    tcp->DstPort = htons(destination_port);
    tcp->Syn = syn ? 1 : 0;
    tcp->Ack = ack ? 1 : 0;
}

template <std::size_t N>
void set_payload_prefix(std::array<std::byte, N>& buffer,
                        std::initializer_list<std::uint8_t> prefix)
{
    auto* payload = buffer.data() + 40;
    std::size_t index = 0;
    for (const auto value : prefix) {
        payload[index++] = static_cast<std::byte>(value);
    }
}

template <std::size_t N>
bool filter_matches(const std::string& filter,
                    std::array<std::byte, N>& packet,
                    UINT length,
                    bool outbound)
{
    WINDIVERT_ADDRESS address{};
    address.Outbound = outbound ? 1 : 0;
    return WinDivertHelperEvalFilter(filter.c_str(), packet.data(), length,
                                     &address) != FALSE;
}

void test_filter_matching()
{
    const sdp::Config config;
    const auto filter = sdp::build_main_filter(config);
    Buffer buffer;

    // Outbound 443 requires a TLS ClientHello prefix: every HTTPS trick acts
    // on the ClientHello, so application-data records stay in the kernel.
    make_ipv4_tcp_packet(buffer, true, 50000, 443, 100, false, true, 1);
    CHECK(!filter_matches(filter, buffer, 140, true));

    set_payload_prefix(buffer, {0x16, 0x03, 0x01});
    CHECK(filter_matches(filter, buffer, 140, true));

    set_payload_prefix(buffer, {0x16, 0x03, 0x03});
    CHECK(filter_matches(filter, buffer, 140, true));

    set_payload_prefix(buffer, {0x17, 0x03, 0x03});
    CHECK(!filter_matches(filter, buffer, 140, true));

    set_payload_prefix(buffer, {'G', 'E', 'T', ' '});
    CHECK(!filter_matches(filter, buffer, 140, true));

    // A two-byte ClientHello prefix is accepted by the user-space check.
    make_ipv4_tcp_packet(buffer, true, 50000, 443, 2, false, true, 1);
    set_payload_prefix(buffer, {0x16, 0x03});
    CHECK(filter_matches(filter, buffer, 42, true));

    make_ipv4_tcp_packet(buffer, true, 50000, 443, 1, false, true, 1);
    set_payload_prefix(buffer, {0x16});
    CHECK(!filter_matches(filter, buffer, 41, true));

    // Empty payloads never match.
    make_ipv4_tcp_packet(buffer, true, 50000, 443, 0, false, true, 1);
    CHECK(!filter_matches(filter, buffer, 40, true));

    // Port 80 remains content-agnostic.
    make_ipv4_tcp_packet(buffer, true, 50000, 80, 100, false, true, 1);
    set_payload_prefix(buffer, {0x17, 0x03, 0x03});
    CHECK(filter_matches(filter, buffer, 140, true));

    make_ipv4_tcp_packet(buffer, false, 80, 50000, 17, false, true, 1);
    CHECK(filter_matches(filter, buffer, 57, false));

    make_ipv4_tcp_packet(buffer, false, 80, 50000, 16, false, true, 1);
    CHECK(!filter_matches(filter, buffer, 56, false));

    make_ipv4_tcp_packet(buffer, false, 80, 50000, 0, false, true, 1);
    CHECK(!filter_matches(filter, buffer, 40, false));

    make_ipv4_tcp_packet(buffer, false, 443, 50000, 0, true, true, 1);
    CHECK(filter_matches(filter, buffer, 40, false));

    make_ipv4_tcp_packet(buffer, false, 443, 50000, 100, false, true, 1);
    CHECK(!filter_matches(filter, buffer, 140, false));

    // Large ClientHellos still pass the max-payload gate; large non-TLS
    // records do not.
    sdp::Config limited = config;
    limited.max_payload = 1200;
    const auto limited_filter = sdp::build_main_filter(limited);
    std::array<std::byte, 2048> large{};
    make_ipv4_tcp_packet(large, true, 50000, 443, 1500, false, true, 1);
    set_payload_prefix(large, {0x16, 0x03, 0x01});
    CHECK(filter_matches(limited_filter, large, 1540, true));

    make_ipv4_tcp_packet(large, true, 50000, 443, 1500, false, true, 1);
    set_payload_prefix(large, {0x17, 0x03, 0x03});
    CHECK(!filter_matches(limited_filter, large, 1540, true));

    make_ipv4_tcp_packet(large, true, 50000, 80, 1500, false, true, 1);
    set_payload_prefix(large, {'G', 'E', 'T', ' '});
    CHECK(filter_matches(limited_filter, large, 1540, true));
}

void check_parse(Buffer& buffer,
                 UINT length,
                 const char* name,
                 int iteration = -1)
{
    const auto actual = sdp::win::parse_packet(buffer.data(), length);

    PWINDIVERT_IPHDR ipv4 = nullptr;
    PWINDIVERT_IPV6HDR ipv6 = nullptr;
    PWINDIVERT_TCPHDR tcp = nullptr;
    PWINDIVERT_UDPHDR udp = nullptr;
    PVOID payload = nullptr;
    UINT payload_length = 0;
    PVOID next = nullptr;
    UINT next_length = 0;
    const BOOL ok = WinDivertHelperParsePacket(
        buffer.data(), length, &ipv4, &ipv6, nullptr, nullptr, nullptr, &tcp,
        &udp, &payload, &payload_length, &next, &next_length);

    const auto label = [&]() -> std::ostream& {
        std::cerr << name;
        if (iteration >= 0) {
            std::cerr << " (iteration " << iteration << ')';
        }
        return std::cerr;
    };

    if (!ok) {
        if (actual.ipv4 != nullptr || actual.ipv6 != nullptr ||
            actual.tcp != nullptr || actual.udp != nullptr ||
            actual.payload != nullptr || actual.payload_length != 0) {
            label() << ": parser accepted a packet WinDivert rejects\n";
            ++failures;
        }
        return;
    }

    if (actual.ipv4 != ipv4 || actual.ipv6 != ipv6 ||
        actual.tcp != tcp || actual.udp != udp ||
        actual.payload != static_cast<std::byte*>(payload) ||
        actual.payload_length != payload_length) {
        const auto offset_of = [&buffer](const void* pointer) {
            return pointer == nullptr
                       ? -1LL
                       : static_cast<long long>(
                             static_cast<const std::byte*>(pointer) -
                             buffer.data());
        };

        label() << ": parsed packet does not match WinDivert\n"
                << "  length:         " << length << '\n'
                << "  ipv4:           " << offset_of(actual.ipv4) << " vs "
                << offset_of(ipv4) << '\n'
                << "  ipv6:           " << offset_of(actual.ipv6) << " vs "
                << offset_of(ipv6) << '\n'
                << "  tcp:            " << offset_of(actual.tcp) << " vs "
                << offset_of(tcp) << '\n'
                << "  udp:            " << offset_of(actual.udp) << " vs "
                << offset_of(udp) << '\n'
                << "  payload:        " << offset_of(actual.payload)
                << " vs " << offset_of(payload) << '\n'
                << "  payload length: " << actual.payload_length << " vs "
                << payload_length << '\n';
        ++failures;
    }
}

void test_packet_parsing_edges()
{
    Buffer buffer;

    const auto null_packet = sdp::win::parse_packet(nullptr, 100);
    CHECK(null_packet.ipv4 == nullptr);
    CHECK(null_packet.tcp == nullptr);
    CHECK(null_packet.payload == nullptr);

    make_ipv4_tcp(buffer, 100, 140);
    check_parse(buffer, 0, "empty packet");

    // Zero-length payloads are reported as null with a zero length, which the
    // fast path must reproduce.
    make_ipv4_tcp(buffer, 0, 40);
    check_parse(buffer, 40, "IPv4 TCP without payload");

    make_ipv6_tcp(buffer, 0);
    check_parse(buffer, 60, "IPv6 TCP without payload");

    make_ipv4_udp(buffer, 0, 28);
    check_parse(buffer, 28, "IPv4 UDP without payload");

    make_ipv6_udp(buffer, 0);
    check_parse(buffer, 48, "IPv6 UDP without payload");

    make_ipv6_udp(buffer, 100);
    check_parse(buffer, 148, "IPv6 UDP");

    make_ipv6_udp(buffer, 100);
    check_parse(buffer, 160, "IPv6 UDP with trailing bytes");

    make_ipv6_tcp_hopopts(buffer, 100);
    check_parse(buffer, 168, "IPv6 TCP with hop-by-hop options");

    // Fragments must never be parsed as a complete transport segment.
    make_ipv4_tcp(buffer, 100, 140);
    reinterpret_cast<PWINDIVERT_IPHDR>(buffer.data())->FragOff0 =
        htons(0x2000); // More fragments, offset 0
    check_parse(buffer, 140, "IPv4 TCP with more-fragments flag");

    make_ipv4_tcp(buffer, 100, 140);
    reinterpret_cast<PWINDIVERT_IPHDR>(buffer.data())->FragOff0 =
        htons(0x0010); // Fragment offset 2
    check_parse(buffer, 140, "IPv4 fragmented TCP");

    make_ipv4_icmp(buffer);
    check_parse(buffer, 28, "IPv4 ICMP");

    make_ipv6_icmp(buffer);
    check_parse(buffer, 48, "IPv6 ICMPv6");

    // IP total length of zero (some offload paths leave it unset).
    make_ipv4_tcp(buffer, 100, 140);
    reinterpret_cast<PWINDIVERT_IPHDR>(buffer.data())->Length = 0;
    check_parse(buffer, 140, "IPv4 TCP with zero total length");

    // Maximum IPv4 header length.
    make_ipv4_tcp(buffer, 100, 160, 15);
    check_parse(buffer, 160, "IPv4 TCP with 60-byte IP header");
}

void check_rebase(Buffer& source, UINT length, const char* name)
{
    const auto parsed = sdp::win::parse_packet(source.data(), length);

    Buffer copy;
    std::memcpy(copy.data(), source.data(), length);
    const auto rebased =
        sdp::win::rebase_packet(parsed, source.data(), copy.data());
    const auto fresh = sdp::win::parse_packet(copy.data(), length);

    if (rebased.ipv4 != fresh.ipv4 || rebased.ipv6 != fresh.ipv6 ||
        rebased.tcp != fresh.tcp || rebased.udp != fresh.udp ||
        rebased.payload != fresh.payload ||
        rebased.payload_length != fresh.payload_length) {
        std::cerr << name << ": rebased packet does not match a fresh parse\n";
        ++failures;
    }
}

void test_packet_rebasing()
{
    Buffer buffer;

    make_ipv4_tcp(buffer, 100, 140);
    check_rebase(buffer, 140, "rebased IPv4 TCP");

    make_ipv4_tcp(buffer, 0, 40);
    check_rebase(buffer, 40, "rebased IPv4 TCP without payload");

    make_ipv6_tcp(buffer, 100);
    check_rebase(buffer, 160, "rebased IPv6 TCP");

    make_ipv4_udp(buffer, 100, 128);
    check_rebase(buffer, 128, "rebased IPv4 UDP");

    make_ipv4_tcp(buffer, 100, 140);
    check_rebase(buffer, 20, "rebased unparseable packet");
}

void test_packet_parsing_fuzz()
{
    std::mt19937 rng(0xC0FFEEU);
    Buffer buffer;

    constexpr int kIterations = 20000;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        const UINT length = static_cast<UINT>(rng() % 201U);
        for (UINT index = 0; index < length; ++index) {
            buffer[index] = static_cast<std::byte>(rng() & 0xFFU);
        }

        // Three out of four packets get a plausible IP header so the fast
        // path is exercised with near-miss lengths, protocols and offsets.
        if (length >= 40 && (rng() % 4U) != 0) {
            if ((rng() % 3U) == 0) {
                auto* ip =
                    reinterpret_cast<PWINDIVERT_IPV6HDR>(buffer.data());
                const auto payload_length =
                    (rng() % 2U) == 0
                        ? static_cast<UINT16>(length - 40)
                        : static_cast<UINT16>(rng());
                ip->Version = 6;
                ip->Length = htons(payload_length);

                constexpr std::uint8_t kNextHeaders[] = {0,  6,  17, 43,
                                                         44, 51, 58, 60};
                ip->NextHdr = kNextHeaders[rng() % 8U];
            }
            else {
                auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(buffer.data());
                const auto ip_header_words =
                    static_cast<std::uint8_t>(5U + (rng() % 4U));
                const auto total_length =
                    (rng() % 2U) == 0 ? static_cast<UINT16>(length)
                                      : static_cast<UINT16>(rng());
                ip->Version = 4;
                ip->HdrLength = ip_header_words;
                ip->Length = htons(total_length);

                constexpr std::uint8_t kProtocols[] = {IPPROTO_TCP,
                                                       IPPROTO_UDP,
                                                       IPPROTO_ICMP};
                ip->Protocol = kProtocols[rng() % 3U];
                ip->FragOff0 = (rng() % 2U) == 0
                                   ? 0
                                   : htons(static_cast<UINT16>(rng()));

                const UINT transport_offset =
                    static_cast<UINT>(ip_header_words) * 4U;
                if ((rng() % 2U) == 0 &&
                    transport_offset + 20U <= length) {
                    auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(
                        buffer.data() + transport_offset);
                    tcp->HdrLength =
                        static_cast<std::uint8_t>(5U + (rng() % 3U));
                    tcp->Ack = 1;
                }
            }
        }

        check_parse(buffer, length, "fuzz", iteration);
    }
}

void test_packet_parsing()
{
    Buffer buffer;

    make_ipv4_tcp(buffer, 100, 140);
    check_parse(buffer, 140, "IPv4 TCP");

    make_ipv4_tcp(buffer, 100, 140);
    check_parse(buffer, 160, "IPv4 TCP with trailing bytes");

    make_ipv4_tcp(buffer, 100, 144, 6);
    check_parse(buffer, 144, "IPv4 TCP with IP options");

    make_ipv4_tcp(buffer, 100, 144, 5, 6);
    check_parse(buffer, 144, "IPv4 TCP with TCP options");

    make_ipv4_tcp(buffer, 100, 140);
    check_parse(buffer, 120, "truncated IPv4 TCP");

    make_ipv4_udp(buffer, 100, 128);
    check_parse(buffer, 128, "IPv4 UDP");

    make_ipv4_udp(buffer, 100, 128);
    check_parse(buffer, 160, "IPv4 UDP with trailing bytes");

    make_ipv6_tcp(buffer, 100);
    check_parse(buffer, 160, "IPv6 TCP");

    make_ipv6_tcp(buffer, 100);
    check_parse(buffer, 140, "truncated IPv6 TCP");
}

void test_engine_http_rewrites()
{
    const auto request = byte_string(
        "GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: test\r\n\r\n");
    const sdp::Logger logger{false};

    {
        sdp::Config config;
        config.http.replace_host = true;
        RecordingSink sink;
        sdp::win::PacketEngine engine{config, logger, sink};

        sdp::win::PacketBuffer packet;
        make_tcp_payload_packet(packet, true, 50000, 80, request);

        const auto action = engine.process(packet);
        CHECK(action.reinject);
        CHECK(action.recalculate_checksums);
        CHECK(payload_string(packet).find("\r\nhoSt: example.com") !=
              std::string::npos);
        CHECK(sink.packets.empty());
    }

    {
        sdp::Config config;
        config.http.mixed_case_host = true;
        RecordingSink sink;
        sdp::win::PacketEngine engine{config, logger, sink};

        sdp::win::PacketBuffer packet;
        make_tcp_payload_packet(packet, true, 50000, 80, request);

        const auto action = engine.process(packet);
        CHECK(action.recalculate_checksums);
        CHECK(payload_string(packet).find("Host: eXaMpLe.cOm") !=
              std::string::npos);
    }

    {
        sdp::Config config;
        config.http.remove_host_space = true;
        RecordingSink sink;
        sdp::win::PacketEngine engine{config, logger, sink};

        sdp::win::PacketBuffer packet;
        make_tcp_payload_packet(packet, true, 50000, 80, request);

        const auto action = engine.process(packet);
        CHECK(action.recalculate_checksums);
        const auto text = payload_string(packet);
        CHECK(text.find("\r\nHost:example.com\r\n") != std::string::npos);
        CHECK(text.find("User-Agent: test \r\n") != std::string::npos);
    }

    {
        sdp::Config config;
        config.http.replace_host = true;
        RecordingSink sink;
        sdp::win::PacketEngine engine{config, logger, sink};

        sdp::win::PacketBuffer packet;
        make_tcp_payload_packet(
            packet, true, 50000, 80,
            byte_string("GET / HTTP/1.1\r\nUser-Agent: test\r\n\r\n"));

        const auto action = engine.process(packet);
        CHECK(action.reinject);
        CHECK(!action.recalculate_checksums);
    }

    {
        // HTTP tricks stay limited to port 80 unless --http-allports is set.
        sdp::Config config;
        config.http.replace_host = true;
        RecordingSink sink;
        sdp::win::PacketEngine engine{config, logger, sink};

        sdp::win::PacketBuffer packet;
        make_tcp_payload_packet(packet, true, 50000, 8080, request);

        const auto action = engine.process(packet);
        CHECK(action.reinject);
        CHECK(!action.recalculate_checksums);
    }
}

void test_engine_native_fragments()
{
    sdp::Config config;
    config.native_frag = true;
    config.http.fragment_enabled = true;
    config.http.fragment_size = 10;
    // Gating flag: enables the HTTP path without touching the payload.
    config.http.persistent = true;

    const sdp::Logger logger{false};
    RecordingSink sink;
    sdp::win::PacketEngine engine{config, logger, sink};

    const auto request = byte_string(
        "GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: test\r\n\r\n");
    sdp::win::PacketBuffer packet;
    make_tcp_payload_packet(packet, true, 50000, 80, request);
    const UINT original_length = packet.length;

    const auto action = engine.process(packet);
    CHECK(!action.reinject);
    CHECK(sink.packets.size() == 2);

    auto expected = parse_packet_buffer(packet);
    CHECK(expected.payload_length == static_cast<UINT>(request.size()));

    auto head = parse_recorded(sink.packets[0]);
    auto tail = parse_recorded(sink.packets[1]);

    CHECK(sink.packets[0].length == 50);
    CHECK(sink.packets[1].length == original_length - 10);
    CHECK(head.payload_length == 10);
    CHECK(tail.payload_length == static_cast<UINT>(request.size()) - 10);
    CHECK(std::memcmp(head.payload, expected.payload, 10) == 0);
    CHECK(std::memcmp(tail.payload, expected.payload + 10,
                      tail.payload_length) == 0);
    CHECK(ntohl(head.tcp->SeqNum) == 1000);
    CHECK(ntohl(tail.tcp->SeqNum) == 1010);
}

void test_engine_standalone_native_frag()
{
    // -n alone must split both HTTP and HTTPS.
    char program[] = "shadowdpi.exe";
    char flag[] = "-n";
    char* argv[] = {program, flag};
    const sdp::Config config = sdp::parse_cli(2, argv);

    const sdp::Logger logger{false};
    RecordingSink sink;
    sdp::win::PacketEngine engine{config, logger, sink};

    const auto request = byte_string(
        "GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: test\r\n\r\n");
    sdp::win::PacketBuffer http_packet;
    make_tcp_payload_packet(http_packet, true, 50000, 80, request);
    CHECK(!engine.process(http_packet).reinject);
    CHECK(sink.packets.size() == 2);

    sink.packets.clear();
    std::array<std::byte, 32> client_hello{};
    client_hello[0] = std::byte{0x16};
    client_hello[1] = std::byte{0x03};
    client_hello[2] = std::byte{0x01};
    sdp::win::PacketBuffer tls_packet;
    make_tcp_payload_packet(tls_packet, true, 50001, 443, client_hello);
    CHECK(!engine.process(tls_packet).reinject);
    CHECK(sink.packets.size() == 2);
}

void test_engine_fragment_flags()
{
    sdp::Config config;
    config.native_frag = true;
    config.http.fragment_enabled = true;
    config.http.fragment_size = 10;
    config.http.persistent = true;

    const sdp::Logger logger{false};
    RecordingSink sink;
    sdp::win::PacketEngine engine{config, logger, sink};

    const auto request = byte_string(
        "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    sdp::win::PacketBuffer packet;
    make_tcp_payload_packet(packet, true, 50000, 80, request);

    auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(packet.data.data() + 20);
    tcp->Fin = 1;
    tcp->Urg = 1;
    tcp->Psh = 1;
    tcp->UrgPtr = htons(3);

    const auto action = engine.process(packet);
    CHECK(!action.reinject);
    CHECK(sink.packets.size() == 2);

    auto head = parse_recorded(sink.packets[0]);
    auto tail = parse_recorded(sink.packets[1]);
    CHECK(head.tcp->Fin == 0);
    CHECK(head.tcp->Urg == 0);
    CHECK(head.tcp->Psh == 0);
    CHECK(tail.tcp->Fin == 1);
    CHECK(tail.tcp->Urg == 1);
    CHECK(tail.tcp->Psh == 1);
}

void test_engine_send_failure()
{
    sdp::Config config;
    config.native_frag = true;
    config.http.fragment_enabled = true;
    config.http.fragment_size = 10;
    config.http.persistent = true;

    const sdp::Logger logger{false};
    RecordingSink sink;
    sink.fail_sends = true;
    sdp::win::PacketEngine engine{config, logger, sink};

    const auto request = byte_string(
        "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    sdp::win::PacketBuffer packet;
    make_tcp_payload_packet(packet, true, 50000, 80, request);

    const auto action = engine.process(packet);
    // Nothing reached the wire, so the original packet is reinjected.
    CHECK(action.reinject);
    CHECK(sink.packets.empty());
}

void test_engine_fake_packet()
{
    sdp::Config config;
    config.fake.enabled = true;
    config.fake.fixed_ttl = std::uint8_t{5};

    const sdp::Logger logger{false};
    RecordingSink sink;
    sdp::win::PacketEngine engine{config, logger, sink};

    std::array<std::byte, 32> client_hello{};
    client_hello[0] = std::byte{0x16};
    client_hello[1] = std::byte{0x03};
    client_hello[2] = std::byte{0x01};

    sdp::win::PacketBuffer packet;
    make_tcp_payload_packet(packet, true, 50000, 443, client_hello);

    const auto action = engine.process(packet);
    CHECK(action.reinject);
    CHECK(!action.recalculate_checksums);
    CHECK(sink.packets.size() == 1);
    CHECK(packet.length == 40 + static_cast<UINT>(client_hello.size()));

    auto fake = parse_recorded(sink.packets[0]);
    const auto expected_payload = sdp::default_https_fake();
    CHECK(fake.ipv4 != nullptr);
    CHECK(fake.ipv4->TTL == 5);
    CHECK(fake.payload_length ==
          static_cast<UINT>(expected_payload.size()));
    CHECK(sink.packets[0].length ==
          40 + static_cast<UINT>(expected_payload.size()));
    CHECK(std::memcmp(fake.payload, expected_payload.data(),
                      expected_payload.size()) == 0);
}

void test_fake_sender_send_failure()
{
    const auto request =
        byte_string("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    sdp::win::PacketBuffer packet;
    make_tcp_payload_packet(packet, true, 50000, 80, request);
    const auto parsed = parse_packet_buffer(packet);

    RecordingSink sink;
    sink.fail_sends = true;
    sdp::win::PacketBuffer scratch;

    const bool sent = sdp::win::send_fake_packet(
        sink, scratch, packet, parsed, sdp::default_http_fake(),
        sdp::win::FakePacketOptions{5, false, false});
    CHECK(!sent);
    CHECK(sink.packets.empty());
}

void test_engine_dns_redirect()
{
    sdp::Config config;
    config.dns.v4_server = sdp::parse_ip_address("9.9.9.9");
    config.dns.v4_port = 5353;

    const sdp::Logger logger{false};
    RecordingSink sink;
    sdp::win::PacketEngine engine{config, logger, sink};

    const auto query = dns_query_payload();
    sdp::win::PacketBuffer outbound;
    make_udp_payload_packet(outbound, true, ipv4_address(192, 168, 1, 2),
                            ipv4_address(8, 8, 8, 8), 53000, 53, query);

    const auto outgoing_action = engine.process(outbound);
    CHECK(outgoing_action.reinject);
    CHECK(outgoing_action.recalculate_checksums);

    auto parsed = parse_packet_buffer(outbound);
    CHECK(parsed.ipv4->DstAddr == ipv4_address(9, 9, 9, 9));
    CHECK(ntohs(parsed.udp->DstPort) == 5353);

    const auto response = dns_response_payload();

    // A response from the wrong source must not consume the tracker entry.
    sdp::win::PacketBuffer wrong_source;
    make_udp_payload_packet(wrong_source, false, ipv4_address(8, 8, 8, 8),
                            ipv4_address(192, 168, 1, 2), 53, 53000,
                            response);
    CHECK(!engine.process(wrong_source).reinject);

    // A response with the wrong transaction ID is dropped and the entry stays.
    auto wrong_id = response;
    wrong_id[0] = std::byte{0x99};
    sdp::win::PacketBuffer mismatched;
    make_udp_payload_packet(mismatched, false, ipv4_address(9, 9, 9, 9),
                            ipv4_address(192, 168, 1, 2), 5353, 53000,
                            wrong_id);
    CHECK(!engine.process(mismatched).reinject);

    // A response with the wrong question is dropped and the entry stays.
    auto wrong_question = response;
    wrong_question[13] = std::byte{'E'}; // "Example.com" is fine...
    wrong_question[19] = std::byte{'z'}; // ...but this is not the same name.
    sdp::win::PacketBuffer wrong_name;
    make_udp_payload_packet(wrong_name, false, ipv4_address(9, 9, 9, 9),
                            ipv4_address(192, 168, 1, 2), 5353, 53000,
                            wrong_question);
    CHECK(!engine.process(wrong_name).reinject);

    // The real response from the configured resolver is accepted.
    sdp::win::PacketBuffer inbound;
    make_udp_payload_packet(inbound, false, ipv4_address(9, 9, 9, 9),
                            ipv4_address(192, 168, 1, 2), 5353, 53000,
                            response);

    const auto incoming_action = engine.process(inbound);
    CHECK(incoming_action.reinject);
    CHECK(incoming_action.recalculate_checksums);

    parsed = parse_packet_buffer(inbound);
    CHECK(parsed.ipv4->SrcAddr == ipv4_address(8, 8, 8, 8));
    CHECK(ntohs(parsed.udp->SrcPort) == 53);
    CHECK(ntohs(parsed.udp->DstPort) == 53000);

    // The tracker entry was consumed, so a second response is dropped.
    sdp::win::PacketBuffer unmatched;
    make_udp_payload_packet(unmatched, false, ipv4_address(9, 9, 9, 9),
                            ipv4_address(192, 168, 1, 2), 5353, 53000,
                            response);
    const auto unmatched_action = engine.process(unmatched);
    CHECK(!unmatched_action.reinject);
}

void test_engine_inbound_window_size()
{
    const sdp::Logger logger{false};

    {
        sdp::Config config;
        config.http.fragment_enabled = true;
        config.http.fragment_size = 40;
        RecordingSink sink;
        sdp::win::PacketEngine engine{config, logger, sink};

        sdp::win::PacketBuffer packet;
        make_tcp_control_packet(packet, false, 80, 50000, true, true);

        const auto action = engine.process(packet);
        CHECK(action.reinject);
        CHECK(action.recalculate_checksums);
        const auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(
            packet.data.data() + 20);
        CHECK(ntohs(tcp->Window) == 40);
    }

    {
        sdp::Config config;
        config.https.fragment_enabled = true;
        config.https.fragment_size = 30;
        RecordingSink sink;
        sdp::win::PacketEngine engine{config, logger, sink};

        sdp::win::PacketBuffer packet;
        make_tcp_control_packet(packet, false, 443, 50000, true, true);

        const auto action = engine.process(packet);
        CHECK(action.recalculate_checksums);
        const auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(
            packet.data.data() + 20);
        CHECK(ntohs(tcp->Window) == 30);
    }

    {
        // Outbound handshakes are never modified.
        sdp::Config config;
        config.http.fragment_enabled = true;
        config.http.fragment_size = 40;
        RecordingSink sink;
        sdp::win::PacketEngine engine{config, logger, sink};

        sdp::win::PacketBuffer packet;
        make_tcp_control_packet(packet, true, 50000, 80, true, false);

        const auto action = engine.process(packet);
        CHECK(action.reinject);
        CHECK(!action.recalculate_checksums);
        const auto* tcp = reinterpret_cast<PWINDIVERT_TCPHDR>(
            packet.data.data() + 20);
        CHECK(ntohs(tcp->Window) == 65535);
    }
}

void test_engine_passive_dpi()
{
    sdp::Config config;
    config.block_passive = true;

    const sdp::Logger logger{false};
    RecordingSink sink;
    sdp::win::PacketEngine engine{config, logger, sink};

    const auto redirect = byte_string(
        "HTTP/1.1 302 Found\r\nLocation: http://blocked.example/\r\n"
        "Connection: close\r\n\r\n");

    sdp::win::PacketBuffer packet;
    make_tcp_payload_packet(packet, false, 80, 50000, redirect);

    const auto action = engine.process(packet);
    CHECK(!action.reinject);
    CHECK(!action.recalculate_checksums);

    sdp::win::PacketBuffer ordinary;
    make_tcp_payload_packet(ordinary, false, 80, 50000,
                            byte_string("HTTP/1.1 200 OK\r\n\r\n"));

    const auto ordinary_action = engine.process(ordinary);
    CHECK(ordinary_action.reinject);
}

void test_copy_packet()
{
    sdp::win::PacketBuffer source;
    sdp::win::PacketBuffer destination;

    source.length = 100;
    source.address.Outbound = 1;
    for (std::size_t index = 0; index < source.length; ++index) {
        source.data[index] = static_cast<std::byte>(index);
    }

    destination.data[100] = std::byte{0x7f};
    sdp::win::copy_packet(destination, source);

    CHECK(destination.length == source.length);
    CHECK(destination.address.Outbound == source.address.Outbound);
    CHECK(std::memcmp(destination.data.data(), source.data.data(),
                      source.length) == 0);
    CHECK(destination.data[100] == std::byte{0x7f});

    // A future caller must not leave a logical length larger than the buffer.
    source.length = static_cast<UINT>(sdp::kMaxPacketSize + 1);
    sdp::win::copy_packet(destination, source);
    CHECK(destination.length == sdp::kMaxPacketSize);
}

} // namespace

int main()
{
    std::string load_error;
    if (!sdp::win::load_windivert_api(load_error)) {
        std::cerr << load_error << '\n';
        return 1;
    }

    test_filters_compile();
    test_filter_matching();
    test_packet_parsing();
    test_packet_parsing_edges();
    test_packet_rebasing();
    test_packet_parsing_fuzz();
    test_engine_http_rewrites();
    test_engine_native_fragments();
    test_engine_standalone_native_frag();
    test_engine_fragment_flags();
    test_engine_send_failure();
    test_engine_fake_packet();
    test_fake_sender_send_failure();
    test_engine_dns_redirect();
    test_engine_inbound_window_size();
    test_engine_passive_dpi();
    test_copy_packet();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "windows tests passed\n";
    return 0;
}
