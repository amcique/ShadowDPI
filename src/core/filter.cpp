#include "shadowdpi/filter.hpp"

#include <set>
#include <string>

#include "shadowdpi/types.hpp"
#include "shadowdpi/util.hpp"

namespace sdp {
namespace {

// A TLS ClientHello starts with a handshake record: 0x16, a 0x03 legacy
// version byte, then a version <= TLS 1.2 (0x03). Every HTTPS trick in the
// engine only acts on the ClientHello, so non-handshake records can stay in
// the kernel. The two-byte case mirrors the user-space prefix check, which
// also accepts a ClientHello split right after the record version.
constexpr std::string_view kTlsHandshakePrefix =
    "((tcp.PayloadLength == 2 and tcp.Payload[0] == 0x16 and "
    "tcp.Payload[1] == 0x03) or "
    "(tcp.PayloadLength >= 3 and tcp.Payload[0] == 0x16 and "
    "tcp.Payload[1] == 0x03 and tcp.Payload[2] <= 0x03))";

// Pure ACKs and tiny payloads are ignored by the packet processor, so keep
// them in the kernel instead of paying a WinDivert receive/send round trip.
// Busy HTTP/HTTPS connections carry one such ACK per data segment, which makes
// this the single largest reduction in intercepted traffic.
constexpr std::string_view kMainFilterTemplate =
    "(tcp and !impostor and !loopback #MAXPAYLOADSIZE# and "
    "((inbound and ("
    "("
    "((ipv6 or (ip.Id >= 0x0 and ip.Id <= 0xF) #IPID#) and "
    "tcp.SrcPort == 80 and tcp.Ack and "
    "tcp.PayloadLength > #MINPAYLOAD#"
    ") or "
    "((tcp.SrcPort == 80 or tcp.SrcPort == 443) and tcp.Ack and tcp.Syn)"
    ")"
    " and (#NO_LOCALNETS_V4_SRC# or #NO_LOCALNETS_V6_SRC#))) or "
    "(outbound and ("
    "((tcp.DstPort == 80) and tcp.Ack and tcp.PayloadLength > 0) or "
    "((tcp.DstPort == 443) and tcp.Ack and tcp.PayloadLength > 0 and "
    "#TLSHANDSHAKEPREFIX#)"
    ") and (#NO_LOCALNETS_V4_DST# or #NO_LOCALNETS_V6_DST#))"
    "))";

constexpr std::string_view kPassiveFilterTemplate =
    "inbound and ip and tcp and !impostor and !loopback and "
    "(true #IPID#) and "
    "(tcp.SrcPort == 443 or tcp.SrcPort == 80) and tcp.Rst and "
    "#NO_LOCALNETS_V4_SRC#";

constexpr std::string_view kNoLocalNetsV4Source =
    "((ip.SrcAddr < 127.0.0.1 or ip.SrcAddr > 127.255.255.255) and "
    "(ip.SrcAddr < 10.0.0.0 or ip.SrcAddr > 10.255.255.255) and "
    "(ip.SrcAddr < 192.168.0.0 or ip.SrcAddr > 192.168.255.255) and "
    "(ip.SrcAddr < 172.16.0.0 or ip.SrcAddr > 172.31.255.255) and "
    "(ip.SrcAddr < 169.254.0.0 or ip.SrcAddr > 169.254.255.255))";

constexpr std::string_view kNoLocalNetsV4Destination =
    "((ip.DstAddr < 127.0.0.1 or ip.DstAddr > 127.255.255.255) and "
    "(ip.DstAddr < 10.0.0.0 or ip.DstAddr > 10.255.255.255) and "
    "(ip.DstAddr < 192.168.0.0 or ip.DstAddr > 192.168.255.255) and "
    "(ip.DstAddr < 172.16.0.0 or ip.DstAddr > 172.31.255.255) and "
    "(ip.DstAddr < 169.254.0.0 or ip.DstAddr > 169.254.255.255))";

constexpr std::string_view kNoLocalNetsV6Source =
    "((ipv6.SrcAddr > ::1) and "
    "(ipv6.SrcAddr < 2001::0 or ipv6.SrcAddr > 2001:1::0) and "
    "(ipv6.SrcAddr < fc00::0 or ipv6.SrcAddr > fe00::0) and "
    "(ipv6.SrcAddr < fe80::0 or ipv6.SrcAddr > fec0::0) and "
    "(ipv6.SrcAddr < ff00::0 or ipv6.SrcAddr > ffff::0))";

constexpr std::string_view kNoLocalNetsV6Destination =
    "((ipv6.DstAddr > ::1) and "
    "(ipv6.DstAddr < 2001::0 or ipv6.DstAddr > 2001:1::0) and "
    "(ipv6.DstAddr < fc00::0 or ipv6.DstAddr > fe00::0) and "
    "(ipv6.DstAddr < fe80::0 or ipv6.DstAddr > fec0::0) and "
    "(ipv6.DstAddr < ff00::0 or ipv6.DstAddr > ffff::0))";

std::string local_net_expansion(std::string text)
{
    text = replace_all(std::move(text), "#NO_LOCALNETS_V4_SRC#", kNoLocalNetsV4Source);
    text = replace_all(std::move(text), "#NO_LOCALNETS_V4_DST#", kNoLocalNetsV4Destination);
    text = replace_all(std::move(text), "#NO_LOCALNETS_V6_SRC#", kNoLocalNetsV6Source);
    text = replace_all(std::move(text), "#NO_LOCALNETS_V6_DST#", kNoLocalNetsV6Destination);
    return text;
}

std::string ip_id_expansion(const Config& config)
{
    std::string expression;
    for (const auto id : config.ip_ids) {
        expression += " or ip.Id == " + std::to_string(id);
    }
    return expression;
}

std::string max_payload_expansion(const Config& config)
{
    if (config.max_payload == 0) {
        return {};
    }

    return "and (tcp.PayloadLength ? tcp.PayloadLength < " +
           std::to_string(config.max_payload) +
           " or tcp.Payload32[0] == 0x47455420 "
           "or tcp.Payload32[0] == 0x504F5354 "
           "or #TLSHANDSHAKEPREFIX#: true)";
}

} // namespace

std::string build_main_filter(const Config& config)
{
    std::string filter = local_net_expansion(std::string(kMainFilterTemplate));
    filter = replace_all(std::move(filter), "#MINPAYLOAD#",
                         std::to_string(kMinTcpPayloadSize));

    for (const auto port : config.extra_ports) {
        if (port == kHttpPort || port == kHttpsPort) {
            continue;
        }
        filter += " or (tcp and !impostor and !loopback #MAXPAYLOADSIZE# and "
                  "(tcp.SrcPort == " + std::to_string(port) +
                  " or tcp.DstPort == " + std::to_string(port) + "))";
    }

    filter = replace_all(std::move(filter), "#MAXPAYLOADSIZE#",
                         max_payload_expansion(config));
    filter = replace_all(std::move(filter), "#TLSHANDSHAKEPREFIX#",
                         kTlsHandshakePrefix);
    filter = replace_all(std::move(filter), "#IPID#", ip_id_expansion(config));

    std::set<std::uint16_t> dns_ports;
    if (config.dns.v4_server) {
        dns_ports.insert(53);
        dns_ports.insert(config.dns.v4_port);
    }
    if (config.dns.v6_server) {
        dns_ports.insert(53);
        dns_ports.insert(config.dns.v6_port);
    }

    for (const auto port : dns_ports) {
        filter += " or (udp and !impostor and !loopback and "
                  "(udp.SrcPort == " + std::to_string(port) +
                  " or udp.DstPort == " + std::to_string(port) + "))";
    }

    return filter;
}

std::string build_passive_filter(const Config& config)
{
    std::string filter = local_net_expansion(std::string(kPassiveFilterTemplate));
    return replace_all(std::move(filter), "#IPID#", ip_id_expansion(config));
}

std::string build_quic_filter()
{
    return "outbound and !impostor and !loopback and udp "
           "and udp.DstPort == 443 and udp.PayloadLength >= 1200 "
           "and udp.Payload[0] >= 0xC0 and udp.Payload32[1b] == 0x01";
}

} // namespace sdp
