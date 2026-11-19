#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "shadowdpi/blacklist.hpp"
#include "shadowdpi/config.hpp"
#include "shadowdpi/dns_tracker.hpp"
#include "shadowdpi/log.hpp"
#include "shadowdpi/ttl_tracker.hpp"
#include "packet.hpp"
#include "packet_sink.hpp"

namespace sdp::win {

struct PacketAction {
    bool reinject{true};
    bool recalculate_checksums{false};
};

/// Applies the packet-level DPI circumvention logic.
///
/// The engine holds no driver state: it parses and mutates packets in place,
/// records TTL/DNS state and hands generated packets to a PacketSink. The
/// referenced configuration, logger and sink must outlive the engine. This
/// keeps every decision testable without WinDivert handles or administrator
/// privileges.
class PacketEngine {
public:
    PacketEngine(const Config& config, const Logger& logger, PacketSink& sink);

    /// Installs a loaded blacklist. `enabled` is true when at least one file
    /// was loaded; when it is false every packet is processed.
    void set_blacklist(Blacklist blacklist, bool enabled);

    /// Parses and processes one packet in place. The caller must reinject the
    /// packet when the returned action allows it.
    [[nodiscard]] PacketAction process(PacketBuffer& packet);

private:
    struct TcpDataResult {
        PacketAction action;
        std::optional<std::size_t> sni_split;
    };

    enum class FragmentPart {
        head,
        tail,
    };

    [[nodiscard]] TcpDataResult process_tcp_data(PacketBuffer& packet,
                                                 ParsedPacket& parsed,
                                                 bool outbound);
    [[nodiscard]] TcpDataResult process_inbound_tcp_data(
        std::span<const std::byte> payload, const ParsedPacket& parsed);
    [[nodiscard]] TcpDataResult process_tls_client_hello(
        PacketBuffer& packet,
        ParsedPacket& parsed,
        std::span<const std::byte> payload);
    [[nodiscard]] TcpDataResult process_http_request(
        PacketBuffer& packet,
        ParsedPacket& parsed,
        std::span<std::byte> payload);
    [[nodiscard]] bool any_http_trick_enabled() const noexcept;
    [[nodiscard]] PacketAction process_tcp_control(const ParsedPacket& parsed,
                                                   bool outbound);
    [[nodiscard]] PacketAction process_udp_data(const ParsedPacket& parsed,
                                                bool outbound);
    [[nodiscard]] bool is_expected_dns_source(
        const ParsedPacket& parsed) const noexcept;

    [[nodiscard]] std::uint8_t compute_fake_ttl(const ParsedPacket& parsed,
                                                bool& allow_send);
    bool send_fragments(const PacketBuffer& packet,
                        const ParsedPacket& parsed,
                        const TcpDataResult& result);
    bool send_native_fragment(const PacketBuffer& packet,
                              const ParsedPacket& parsed,
                              unsigned int fragment_size,
                              FragmentPart part);
    void note_send_failure() noexcept;

    const Config& config_;
    const Logger& logger_;
    PacketSink& sink_;
    Blacklist blacklist_;
    bool blacklist_enabled_{false};
    TtlTracker ttl_tracker_;
    DnsTracker dns_tracker_;
    PacketBuffer fake_scratch_;
    PacketBuffer fragment_scratch_;
    std::uint64_t send_failures_{0};
};

} // namespace sdp::win
