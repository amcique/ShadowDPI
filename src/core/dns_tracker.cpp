#include "shadowdpi/dns_tracker.hpp"

#include <functional>
#include <string>
#include <utility>

#include "shadowdpi/util.hpp"

namespace sdp {
namespace {

constexpr std::size_t kDnsHeaderSize = 12;

void hash_combine(std::size_t& seed, std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

[[nodiscard]] unsigned char fold_ascii(unsigned char value) noexcept
{
    if (value >= 'A' && value <= 'Z') {
        return static_cast<unsigned char>(value - 'A' + 'a');
    }
    return value;
}

/// Returns the question as length-prefixed labels plus QTYPE/QCLASS. QNAME
/// bytes are folded for DNS 0x20 case randomization; QTYPE/QCLASS stay exact.
/// Compression pointers in a question are rejected.
[[nodiscard]] std::optional<std::string> normalized_question(
    std::span<const std::byte> payload)
{
    if (payload.size() < kDnsHeaderSize) {
        return std::nullopt;
    }

    std::size_t offset = kDnsHeaderSize;
    std::string question;
    while (true) {
        if (offset >= payload.size()) {
            return std::nullopt;
        }
        const auto length = std::to_integer<std::uint8_t>(payload[offset]);
        question.push_back(static_cast<char>(length));
        if (length == 0) {
            ++offset;
            break;
        }
        if ((length & 0xC0U) != 0 || offset + 1 + length > payload.size()) {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < length; ++index) {
            question.push_back(static_cast<char>(fold_ascii(
                std::to_integer<unsigned char>(payload[offset + 1 + index]))));
        }
        offset += 1 + length;
    }

    if (payload.size() - offset < 4) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        question.push_back(static_cast<char>(
            std::to_integer<unsigned char>(payload[offset + index])));
    }
    return question;
}

} // namespace

std::size_t DnsTracker::KeyHash::operator()(const Key& key) const noexcept
{
    std::size_t seed = std::hash<bool>{}(key.ipv6);
    for (const auto word : key.ip) {
        hash_combine(seed, std::hash<std::uint32_t>{}(word));
    }
    hash_combine(seed, std::hash<std::uint16_t>{}(key.port));
    hash_combine(seed, std::hash<std::uint16_t>{}(key.transaction_id));
    hash_combine(seed, std::hash<std::string>{}(key.question));
    return seed;
}

bool DnsTracker::handle_outgoing(
    const std::array<std::uint32_t, 4>& local_ip,
    std::uint16_t local_port,
    const std::array<std::uint32_t, 4>& remote_ip,
    std::uint16_t remote_port,
    std::span<const std::byte> payload,
    bool ipv6)
{
    cleanup();

    if (!is_dns_packet(payload, true)) {
        return false;
    }
    const auto question = normalized_question(payload);
    if (!question) {
        return false;
    }

    const Key key{ipv6, local_ip, local_port, read_be16(payload.data()),
                  std::move(*question)};

    if (entries_.size() >= kMaxEntries) {
        // Bound memory under a query flood. Evicting any entry is acceptable:
        // the corresponding response will then be treated as unmatched.
        entries_.erase(entries_.begin());
    }

    auto [iterator, inserted] =
        entries_.try_emplace(key, Entry{remote_ip, remote_port, Clock::now()});
    if (!inserted) {
        // A retransmitted query refreshes the original resolver endpoint.
        iterator->second.remote_ip = remote_ip;
        iterator->second.remote_port = remote_port;
        iterator->second.timestamp = Clock::now();
    }
    return true;
}

std::optional<DnsRedirect> DnsTracker::handle_incoming(
    const std::array<std::uint32_t, 4>& local_ip,
    std::uint16_t local_port,
    std::span<const std::byte> payload,
    bool ipv6)
{
    cleanup();

    if (!is_dns_packet(payload, false)) {
        return std::nullopt;
    }
    const auto question = normalized_question(payload);
    if (!question) {
        return std::nullopt;
    }

    const Key key{ipv6, local_ip, local_port, read_be16(payload.data()),
                  std::move(*question)};
    const auto iterator = entries_.find(key);
    if (iterator == entries_.end()) {
        return std::nullopt;
    }

    const DnsRedirect redirect{iterator->second.remote_ip,
                               iterator->second.remote_port};
    entries_.erase(iterator);
    return redirect;
}

bool DnsTracker::is_dns_packet(std::span<const std::byte> payload,
                                bool outgoing)
{
    if (payload.size() < 16) {
        return false;
    }

    const std::uint16_t flags = read_be16(payload.data() + 2);
    if (outgoing) {
        return (flags & 0xFA00) == 0 &&
               read_be16(payload.data() + 4) != 0 &&
               read_be32(payload.data() + 6) == 0;
    }
    return (flags & 0xF800) == 0x8000;
}

void DnsTracker::cleanup()
{
    const auto now = Clock::now();
    if (!last_cleanup_) {
        last_cleanup_ = now;
        return;
    }
    if (now - *last_cleanup_ < kCleanupInterval) {
        return;
    }
    last_cleanup_ = now;

    // Bounded incremental cleanup: never walk the whole map on the packet
    // thread. Rotating over buckets ensures every entry is eventually seen
    // even when the batch is smaller than the map.
    const std::size_t bucket_count = entries_.bucket_count();
    if (bucket_count == 0) {
        return;
    }

    std::size_t inspected = 0;
    std::size_t bucket = next_bucket_ % bucket_count;
    for (std::size_t visited = 0;
         visited < bucket_count && inspected < kCleanupBatch; ++visited) {
        for (auto iterator = entries_.begin(bucket);
             iterator != entries_.end(bucket) &&
             inspected < kCleanupBatch;) {
            ++inspected;
            if (now - iterator->second.timestamp >= kEntryLifetime) {
                iterator = entries_.erase(iterator);
            }
            else {
                ++iterator;
            }
        }
        bucket = (bucket + 1) % bucket_count;
    }
    next_bucket_ = bucket;
}

} // namespace sdp
