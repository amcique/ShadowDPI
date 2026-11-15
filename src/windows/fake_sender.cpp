#include "fake_sender.hpp"

#include <cstring>
#include <span>

#include "shadowdpi/fake_payloads.hpp"

namespace sdp::win {
namespace {

// Large enough for the remote side to treat the fake packet as stale.
constexpr std::uint32_t kAckRewind = 66000;
constexpr std::uint32_t kSequenceRewind = 10000;

} // namespace

bool send_fake_packet(PacketSink& sink,
                      PacketBuffer& scratch,
                      const PacketBuffer& original,
                      const ParsedPacket& parsed,
                      std::span<const std::byte> payload,
                      const FakePacketOptions& options) noexcept
{
    if (parsed.payload == nullptr || payload.empty()) {
        return true;
    }

    if (parsed.payload_length > original.length) {
        return true;
    }

    const UINT new_length = original.length - parsed.payload_length +
                            static_cast<UINT>(payload.size());
    if (new_length > kMaxPacketSize) {
        return true;
    }

    copy_packet(scratch, original);
    ParsedPacket fake_parsed =
        rebase_packet(parsed, original.data.data(), scratch.data.data());

    std::memcpy(fake_parsed.payload, payload.data(), payload.size());
    scratch.length = new_length;
    set_ip_length(fake_parsed, new_length);

    if (options.ttl != 0) {
        if (fake_parsed.ipv4 != nullptr) {
            fake_parsed.ipv4->TTL = options.ttl;
        }
        else if (fake_parsed.ipv6 != nullptr) {
            fake_parsed.ipv6->HopLimit = options.ttl;
        }
    }

    if (options.rewind_sequence && fake_parsed.tcp != nullptr) {
        fake_parsed.tcp->AckNum =
            htonl(ntohl(fake_parsed.tcp->AckNum) - kAckRewind);
        fake_parsed.tcp->SeqNum =
            htonl(ntohl(fake_parsed.tcp->SeqNum) - kSequenceRewind);
    }

    scratch.address.IPChecksum = 0;
    scratch.address.TCPChecksum = 0;
    scratch.address.UDPChecksum = 0;
    recalculate_checksums(scratch.data.data(), scratch.length,
                          scratch.address);

    if (options.damage_checksum && fake_parsed.tcp != nullptr) {
        fake_parsed.tcp->Checksum = htons(
            static_cast<UINT16>(ntohs(fake_parsed.tcp->Checksum) - 1));
    }

    return sink.send(scratch);
}

std::size_t send_fake_requests(PacketSink& sink,
                               PacketBuffer& scratch,
                               const PacketBuffer& packet,
                               const ParsedPacket& parsed,
                               const FakeOptions& options,
                               std::uint8_t fake_ttl,
                               bool https) noexcept
{
    if (options.resend == 0) {
        return 0;
    }

    std::size_t failures = 0;
    const std::size_t count =
        options.payloads.empty() ? 1 : options.payloads.size();

    for (std::size_t index = 0; index < count; ++index) {
        const std::span<const std::byte> payload =
            options.payloads.empty()
                ? (https ? default_https_fake() : default_http_fake())
                : std::span<const std::byte>{options.payloads[index]};

        for (std::uint8_t repeat = 0; repeat < options.resend; ++repeat) {
            if (fake_ttl != 0 &&
                !send_fake_packet(sink, scratch, packet, parsed, payload,
                                  FakePacketOptions{fake_ttl, false, false})) {
                ++failures;
            }
            if (options.wrong_checksum &&
                !send_fake_packet(sink, scratch, packet, parsed, payload,
                                  FakePacketOptions{0, true, false})) {
                ++failures;
            }
            if (options.wrong_seq &&
                !send_fake_packet(sink, scratch, packet, parsed, payload,
                                  FakePacketOptions{0, false, true})) {
                ++failures;
            }
        }
    }
    return failures;
}

} // namespace sdp::win
