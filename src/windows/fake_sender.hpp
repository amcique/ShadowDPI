#pragma once

#include <cstdint>

#include "shadowdpi/config.hpp"
#include "packet.hpp"
#include "packet_sink.hpp"

namespace sdp::win {

struct FakePacketOptions {
    std::uint8_t ttl{0};
    bool damage_checksum{false};
    bool rewind_sequence{false};
};

/// Builds a fake packet from `original`, replaces its TCP payload with
/// `payload`, applies one modification and sends it.
///
/// `scratch` is reused between calls to avoid copying the whole fixed-size
/// packet buffer for every fake packet.
void send_fake_packet(PacketSink& sink,
                      PacketBuffer& scratch,
                      const PacketBuffer& original,
                      const ParsedPacket& parsed,
                      std::span<const std::byte> payload,
                      const FakePacketOptions& options) noexcept;

/// Sends every configured fake payload, replicating the original behavior of
/// sending one packet per enabled modification (TTL, checksum, sequence).
void send_fake_requests(PacketSink& sink,
                        PacketBuffer& scratch,
                        const PacketBuffer& packet,
                        const ParsedPacket& parsed,
                        const FakeOptions& options,
                        std::uint8_t fake_ttl,
                        bool https) noexcept;

} // namespace sdp::win
