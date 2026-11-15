#include "shadowdpi/dns_tracker.hpp"

#include <functional>

#include "shadowdpi/util.hpp"

namespace sdp {
namespace {

constexpr std::size_t kDnsHeaderSize = 12;

void hash_combine(std::size_t& seed, std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

/// Returns the raw question section (QNAME + QTYPE + QCLASS) when the packet
/// contains a complete, uncompressed question. Compression pointers are not
/// valid in the question of a query, so they are rejected.
[[nodiscard]] std::optional<std::span<const std::byte>> question_section(
    std::span<const std::byte> payload)
{
    if (payload.size() < kDnsHeaderSize) {
        return std::nullopt;
    }

    std::size_t offset = kDnsHeaderSize;
    while (true) {
        if (offset >= payload.size()) {
            return std::nullopt;
        }

        const auto length =
            std::to_integer<std::uint8_t>(payload[offset]);
        if (length == 0) {
            ++offset;
            break;
        }
        if ((length & 0xC0U) != 0) {
            return std::nullopt;
        }

        offset += 1 + length;
        if (offset > payload.size()) {
            return std::nullopt;
        }
    }

    if (payload.size() - offset < 4) {
        return std::nullopt;
    }
    offset += 4;

    return payload.subspan(kDnsHeaderSize, offset - kDnsHeaderSize);
}

[[nodiscard]] unsigned char fold_ascii(unsigned char value) noexcept
{
    if (value >= 'A' && value <= 'Z') {
        return static_cast<unsigned char>(value - 'A' + 'a');
    }
    return value;
}

/// DNS names are case-insensitive; QTYPE/QCLASS are compared exactly.
[[nodiscard]] bool questions_match(std::span<const std::byte> left,
                                   std::span<const std::byte> right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }

    std::size_t name_end = 0;
    while (name_end < left.size()) {
        const auto length =
            std::to_integer<std::uint8_t>(left[name_end]);
        if (length == 0) {
            ++name_end;
            break;
        }
        name_end += 1 + length;
        if (name_end > left.size()) {
            return false;
        }
    }

    if (left.size() - name_end < 4) {
        return false;
    }

    for (std::size_t index = 0; index < name_end; ++index) {
        if (fold_ascii(std::to_integer<unsigned char>(left[index])) !=
            fold_ascii(std::to_integer<unsigned char>(right[index]))) {
            return false;
        }
    }
    for (std::size_t index = name_end; index < left.size(); ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

std::size_t DnsTracker::KeyHash::operator()(const Key& key) const noexcept
{
    std::size_t seed = std::hash<bool>{}(key.ipv6);
    for (const auto word : key.ip) {
        hash_combine(seed, std::hash<std::uint32_t>{}(word));
    }
    hash_combine(seed, std::hash<std::uint16_t>{}(key.port));
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

    const auto question = question_section(payload);
    if (!question) {
        return false;
    }

    const std::uint16_t transaction_id = read_be16(payload.data());
    const std::vector<std::byte> question_copy(question->begin(),
                                               question->end());

    if (entries_.size() >= kMaxEntries) {
        // Bound memory under a query flood. Evicting any entry is acceptable:
        // the corresponding response will then be treated as unmatched.
        entries_.erase(entries_.begin());
    }

    const Key key{ipv6, local_ip, local_port};
    auto [iterator, inserted] = entries_.try_emplace(
        key, Entry{remote_ip, remote_port, transaction_id, question_copy,
                   Clock::now()});
    if (!inserted) {
        iterator->second.remote_ip = remote_ip;
        iterator->second.remote_port = remote_port;
        iterator->second.transaction_id = transaction_id;
        iterator->second.question = question_copy;
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

    const Key key{ipv6, local_ip, local_port};
    const auto iterator = entries_.find(key);
    if (iterator == entries_.end()) {
        return std::nullopt;
    }

    if (iterator->second.transaction_id != read_be16(payload.data())) {
        return std::nullopt;
    }

    const auto question = question_section(payload);
    if (!question ||
        !questions_match(iterator->second.question, *question)) {
        return std::nullopt;
    }

    const DnsRedirect redirect{
        iterator->second.remote_ip,
        iterator->second.remote_port,
    };
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
