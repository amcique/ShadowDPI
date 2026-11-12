#include "packet_engine.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "shadowdpi/fake_payloads.hpp"
#include "shadowdpi/http.hpp"
#include "shadowdpi/tls.hpp"
#include "shadowdpi/types.hpp"
#include "shadowdpi/util.hpp"

#include "fake_sender.hpp"

namespace sdp::win {
namespace {

constexpr std::string_view kHostHeader = "\r\nHost: ";
constexpr std::string_view kUserAgentHeader = "\r\nUser-Agent: ";

[[nodiscard]] std::string_view as_string_view(
    std::span<const std::byte> payload,
    std::size_t offset,
    std::size_t length) noexcept
{
    return {reinterpret_cast<const char*>(payload.data() + offset), length};
}

void change_window_size(PWINDIVERT_TCPHDR tcp, unsigned int size)
{
    if (tcp != nullptr && size >= 1 && size <= 0xFFFFu) {
        tcp->Window = htons(static_cast<UINT16>(size));
    }
}

} // namespace

PacketEngine::PacketEngine(const Config& config,
                           const Logger& logger,
                           PacketSink& sink)
    : config_(config)
    , logger_(logger)
    , sink_(sink)
{
}

void PacketEngine::set_blacklist(Blacklist blacklist, bool enabled)
{
    blacklist_ = std::move(blacklist);
    blacklist_enabled_ = enabled;
}

PacketAction PacketEngine::process(PacketBuffer& packet)
{
    ParsedPacket parsed = parse_packet(packet.data.data(), packet.length);
    const bool outbound = packet.address.Outbound != 0;

    if (logger_.verbose_enabled()) {
        logger_.verbose(std::string("received ") +
                        (outbound ? "outbound" : "inbound") + " packet, len=" +
                        std::to_string(packet.length));
    }

    PacketAction action;
    if (parsed.has_tcp_data()) {
        const TcpDataResult result =
            process_tcp_data(packet, parsed, outbound);
        if (result.action.reinject && result.action.recalculate_checksums &&
            config_.native_frag && send_fragments(packet, parsed, result)) {
            // The fragments replace the original packet.
            return PacketAction{.reinject = false};
        }
        action = result.action;
    }
    else if (parsed.has_tcp()) {
        action = process_tcp_control(parsed, outbound);
    }
    else if (parsed.has_udp_data()) {
        action = process_udp_data(parsed, outbound);
    }

    return action;
}

PacketEngine::TcpDataResult PacketEngine::process_tcp_data(
    PacketBuffer& packet, ParsedPacket& parsed, bool outbound)
{
    const auto payload = parsed.payload_span();

    if (!outbound) {
        return process_inbound_tcp_data(payload, parsed);
    }

    const bool non_http_port = parsed.tcp->DstPort != htons(kHttpPort);
    const bool fragment_candidate =
        config_.https.fragment_enabled &&
        payload.size() == config_.https.fragment_size;
    const bool looks_like_tls =
        non_http_port &&
        (fragment_candidate || payload.size() > kMinTcpPayloadSize) &&
        (config_.fake.enabled || config_.native_frag) &&
        is_tls_client_hello_prefix(payload);

    if (looks_like_tls) {
        return process_tls_client_hello(packet, parsed, payload);
    }
    return process_http_request(packet, parsed, payload);
}

PacketEngine::TcpDataResult PacketEngine::process_inbound_tcp_data(
    std::span<const std::byte> payload, const ParsedPacket& parsed)
{
    TcpDataResult result;
    if (payload.size() <= kMinTcpPayloadSize) {
        return result;
    }
    if (!config_.block_passive || !is_passive_dpi_redirect(payload)) {
        return result;
    }

    result.action.reinject = false;
    if (parsed.is_ipv6() &&
        WINDIVERT_IPV6HDR_GET_FLOWLABEL(parsed.ipv6) != 0) {
        // IPv6 flow labels are used to distinguish connections; only drop
        // redirect pages for the default flow label.
        result.action.reinject = true;
    }
    if (logger_.verbose_enabled() && !result.action.reinject) {
        logger_.verbose("dropping passive DPI redirect packet");
    }
    return result;
}

PacketEngine::TcpDataResult PacketEngine::process_tls_client_hello(
    PacketBuffer& packet,
    ParsedPacket& parsed,
    std::span<const std::byte> payload)
{
    TcpDataResult result;

    std::optional<SniLocation> sni;
    if (blacklist_enabled_ || config_.https.by_sni) {
        sni = extract_sni(payload);
    }

    bool should_process = true;
    if (blacklist_enabled_) {
        should_process =
            sni ? blacklist_.matches(sni->hostname) : config_.allow_no_sni;
    }

    if (logger_.verbose_enabled() && sni) {
        logger_.verbose("TLS SNI: " + std::string(sni->hostname));
    }

    if (!should_process) {
        return result;
    }

    if (config_.fake.enabled) {
        bool allow_send = true;
        const std::uint8_t ttl = compute_fake_ttl(parsed, allow_send);
        if (allow_send) {
            send_fake_requests(sink_, fake_scratch_, packet, parsed,
                               config_.fake, ttl, true);
        }
    }

    if (config_.native_frag) {
        result.action.recalculate_checksums = true;
        if (config_.https.by_sni && sni) {
            result.sni_offset = sni->offset;
        }
    }
    return result;
}

bool PacketEngine::any_http_trick_enabled() const noexcept
{
    return config_.http.replace_host || config_.http.remove_host_space ||
           config_.http.mixed_case_host || config_.http.persistent ||
           config_.fake.enabled;
}

PacketEngine::TcpDataResult PacketEngine::process_http_request(
    PacketBuffer& packet,
    ParsedPacket& parsed,
    std::span<std::byte> payload)
{
    TcpDataResult result;
    if (payload.size() <= kMinTcpPayloadSize) {
        return result;
    }

    // Cheap configuration checks first; parsing the request method and the
    // headers is the expensive part of this path.
    if (!any_http_trick_enabled()) {
        return result;
    }
    if (!config_.http.all_ports &&
        parsed.tcp->DstPort != htons(kHttpPort)) {
        return result;
    }

    const unsigned int http_fragment_size =
        config_.http.fragment_enabled ? config_.http.fragment_size : 0;
    const auto method_end =
        find_http_method_end(payload, http_fragment_size, nullptr);
    if (!method_end) {
        return result;
    }

    const auto host = find_header(payload, kHostHeader);
    if (!host) {
        return result;
    }

    if (blacklist_enabled_ &&
        !blacklist_.matches(as_string_view(payload, host->value_offset,
                                           host->value_length))) {
        return result;
    }

    if (config_.native_frag) {
        result.action.recalculate_checksums = true;
    }

    if (config_.fake.enabled) {
        bool allow_send = true;
        const std::uint8_t ttl = compute_fake_ttl(parsed, allow_send);
        if (allow_send) {
            send_fake_requests(sink_, fake_scratch_, packet, parsed,
                               config_.fake, ttl, false);
        }
    }

    if (config_.http.mixed_case_host) {
        mix_case(payload.subspan(host->value_offset, host->value_length));
        result.action.recalculate_checksums = true;
    }

    if (config_.http.replace_host) {
        replace_host_name(payload, *host);
        result.action.recalculate_checksums = true;
    }

    if (config_.http.extra_space && config_.http.remove_host_space) {
        if (*method_end < host->value_offset &&
            add_extra_space_before_uri(payload, *method_end,
                                       host->value_offset)) {
            result.action.recalculate_checksums = true;
        }
    }
    else if (config_.http.remove_host_space) {
        const auto user_agent = find_header(payload, kUserAgentHeader);
        if (user_agent &&
            move_host_space_to_user_agent(payload, *host, *user_agent)) {
            result.action.recalculate_checksums = true;
        }
    }

    return result;
}

PacketAction PacketEngine::process_tcp_control(const ParsedPacket& parsed,
                                               bool outbound)
{
    PacketAction action;
    if (outbound || !parsed.tcp->Syn || !parsed.tcp->Ack) {
        return action;
    }

    if (config_.fake.enabled &&
        (config_.fake.auto_ttl.enabled || config_.fake.min_ttl != 0)) {
        const std::uint8_t ttl = parsed.is_ipv4()
                                     ? parsed.ipv4->TTL
                                     : parsed.ipv6->HopLimit;
        ttl_tracker_.record_incoming(
            source_address(parsed), destination_address(parsed),
            parsed.tcp->SrcPort, parsed.tcp->DstPort, parsed.is_ipv6(), ttl);
    }

    if (!config_.native_frag) {
        if (config_.http.fragment_enabled &&
            parsed.tcp->SrcPort == htons(kHttpPort)) {
            change_window_size(parsed.tcp, config_.http.fragment_size);
            action.recalculate_checksums = true;
        }
        else if (config_.https.fragment_enabled &&
                 parsed.tcp->SrcPort != htons(kHttpPort)) {
            change_window_size(parsed.tcp, config_.https.fragment_size);
            action.recalculate_checksums = true;
        }
    }

    return action;
}

PacketAction PacketEngine::process_udp_data(const ParsedPacket& parsed,
                                            bool outbound)
{
    PacketAction action;
    const auto payload = parsed.payload_span();

    const bool redirect_v4 = parsed.is_ipv4() && config_.dns.v4_server;
    const bool redirect_v6 = parsed.is_ipv6() && config_.dns.v6_server;
    if (!redirect_v4 && !redirect_v6) {
        return action;
    }

    if (!outbound) {
        const auto local_ip = destination_address(parsed);
        const std::uint16_t local_port = parsed.udp->DstPort;
        const auto redirect = dns_tracker_.handle_incoming(
            local_ip, local_port, payload, parsed.is_ipv6());

        if (redirect) {
            if (parsed.is_ipv4()) {
                parsed.ipv4->SrcAddr = redirect->remote_ip[0];
            }
            else {
                store_ipv6(parsed.ipv6->SrcAddr, redirect->remote_ip);
            }
            parsed.udp->SrcPort = redirect->remote_port;
            parsed.udp->DstPort = local_port;
            action.recalculate_checksums = true;
        }
        else if (DnsTracker::is_dns_packet(payload, false)) {
            action.reinject = false;
            if (logger_.verbose_enabled()) {
                logger_.verbose("[DNS] dropping unmatched incoming response");
            }
        }
        return action;
    }

    const bool is_dns = DnsTracker::is_dns_packet(payload, true);
    const bool tracked = dns_tracker_.handle_outgoing(
        source_address(parsed), parsed.udp->SrcPort,
        destination_address(parsed), parsed.udp->DstPort, payload,
        parsed.is_ipv6());

    if (tracked) {
        if (parsed.is_ipv4()) {
            store_ipv4(parsed.ipv4->DstAddr, config_.dns.v4_server->words());
            parsed.udp->DstPort = htons(config_.dns.v4_port);
        }
        else {
            store_ipv6(parsed.ipv6->DstAddr, config_.dns.v6_server->words());
            parsed.udp->DstPort = htons(config_.dns.v6_port);
        }
        action.recalculate_checksums = true;
    }
    else if (is_dns) {
        action.reinject = false;
        if (logger_.verbose_enabled()) {
            logger_.verbose("[DNS] dropping unmatched outgoing request");
        }
    }

    return action;
}

std::uint8_t PacketEngine::compute_fake_ttl(const ParsedPacket& parsed,
                                            bool& allow_send)
{
    allow_send = true;
    std::uint8_t ttl = config_.fake.fixed_ttl.value_or(0);

    if (!config_.fake.auto_ttl.enabled && config_.fake.min_ttl == 0) {
        return ttl;
    }

    const auto observed = ttl_tracker_.lookup_outgoing(
        source_address(parsed), destination_address(parsed),
        parsed.tcp->SrcPort, parsed.tcp->DstPort, parsed.is_ipv6());
    if (!observed) {
        return ttl;
    }

    if (config_.fake.auto_ttl.enabled) {
        const auto computed = TtlTracker::compute_auto_ttl(
            *observed, config_.fake.auto_ttl.first,
            config_.fake.auto_ttl.second, config_.fake.min_ttl,
            config_.fake.auto_ttl.max);
        ttl = computed.value_or(0);

        if (logger_.verbose_enabled()) {
            logger_.verbose("connection TTL = " +
                            std::to_string(*observed) +
                            ", fake TTL = " + std::to_string(ttl));
        }
    }
    else if (config_.fake.min_ttl != 0) {
        if (!TtlTracker::compute_auto_ttl(*observed, 0, 0,
                                          config_.fake.min_ttl, 0)) {
            allow_send = false;
        }
    }

    return ttl;
}

bool PacketEngine::send_fragments(const PacketBuffer& packet,
                                  const ParsedPacket& parsed,
                                  const TcpDataResult& result)
{
    if (parsed.tcp == nullptr) {
        return false;
    }

    unsigned int fragment_size = 0;
    if (config_.http.fragment_enabled &&
        parsed.tcp->DstPort == htons(kHttpPort)) {
        fragment_size = config_.http.fragment_size;
    }
    else if (config_.https.fragment_enabled &&
             parsed.tcp->DstPort != htons(kHttpPort)) {
        if (config_.https.by_sni && result.sni_offset) {
            fragment_size =
                static_cast<unsigned int>(*result.sni_offset);
        }
        else {
            fragment_size = config_.https.fragment_size;
        }
    }

    if (fragment_size == 0) {
        return false;
    }

    if (config_.reverse_frag) {
        send_native_fragment(packet, parsed, fragment_size,
                             FragmentPart::tail);
        send_native_fragment(packet, parsed, fragment_size,
                             FragmentPart::head);
    }
    else {
        send_native_fragment(packet, parsed, fragment_size,
                             FragmentPart::head);
        send_native_fragment(packet, parsed, fragment_size,
                             FragmentPart::tail);
    }
    return true;
}

void PacketEngine::send_native_fragment(const PacketBuffer& packet,
                                        const ParsedPacket& parsed,
                                        unsigned int fragment_size,
                                        FragmentPart part)
{
    copy_packet(fragment_scratch_, packet);
    ParsedPacket fragment = rebase_packet(
        parsed, packet.data.data(), fragment_scratch_.data.data());
    if (!fragment.has_tcp_data()) {
        return;
    }

    const UINT payload_length = fragment.payload_length;
    if (fragment_size >= payload_length) {
        if (part == FragmentPart::tail) {
            fragment_size = 0;
        }
        else {
            return;
        }
    }

    if (part == FragmentPart::head) {
        fragment_scratch_.length =
            packet.length - payload_length + fragment_size;
    }
    else {
        std::memmove(fragment.payload, fragment.payload + fragment_size,
                     payload_length - fragment_size);
        fragment_scratch_.length -= fragment_size;
        fragment.tcp->SeqNum = htonl(
            static_cast<UINT32>(ntohl(fragment.tcp->SeqNum) + fragment_size));
    }

    set_ip_length(fragment, fragment_scratch_.length);
    fragment_scratch_.address.IPChecksum = 0;
    fragment_scratch_.address.TCPChecksum = 0;
    recalculate_checksums(fragment_scratch_.data.data(),
                          fragment_scratch_.length,
                          fragment_scratch_.address);
    sink_.send(fragment_scratch_);
}

} // namespace sdp::win
