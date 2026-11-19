#include "shadowdpi/ttl_tracker.hpp"

#include <algorithm>
#include <functional>

namespace sdp {
namespace {

void hash_combine(std::size_t& seed, std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

} // namespace

std::size_t TtlTracker::KeyHash::operator()(const Key& key) const noexcept
{
    std::size_t seed = std::hash<bool>{}(key.ipv6);
    for (const auto word : key.source.ip) {
        hash_combine(seed, std::hash<std::uint32_t>{}(word));
    }
    hash_combine(seed, std::hash<std::uint16_t>{}(key.source.port));
    for (const auto word : key.destination.ip) {
        hash_combine(seed, std::hash<std::uint32_t>{}(word));
    }
    hash_combine(seed, std::hash<std::uint16_t>{}(key.destination.port));
    return seed;
}

void TtlTracker::record_incoming(
    const std::array<std::uint32_t, 4>& remote_ip,
    const std::array<std::uint32_t, 4>& local_ip,
    std::uint16_t remote_port,
    std::uint16_t local_port,
    bool ipv6,
    std::uint8_t ttl)
{
    cleanup();

    const Key key{
        ipv6,
        Endpoint{remote_ip, remote_port},
        Endpoint{local_ip, local_port},
    };

    if (entries_.size() >= kMaxEntries && entries_.find(key) == entries_.end()) {
        // Bound memory under a SYN/ACK flood. The evicted connection simply
        // loses its auto-TTL hint.
        entries_.erase(entries_.begin());
    }
    const auto [iterator, inserted] =
        entries_.try_emplace(key, Entry{ttl, Clock::now()});
    if (!inserted) {
        // Retransmissions refresh the hop estimate and its timestamp.
        iterator->second.ttl = ttl;
        iterator->second.timestamp = Clock::now();
    }
}

std::optional<std::uint8_t> TtlTracker::lookup_outgoing(
    const std::array<std::uint32_t, 4>& local_ip,
    const std::array<std::uint32_t, 4>& remote_ip,
    std::uint16_t local_port,
    std::uint16_t remote_port,
    bool ipv6)
{
    cleanup();

    const Key key{
        ipv6,
        Endpoint{remote_ip, remote_port},
        Endpoint{local_ip, local_port},
    };

    const auto iterator = entries_.find(key);
    if (iterator == entries_.end()) {
        return std::nullopt;
    }

    const std::uint8_t ttl = iterator->second.ttl;
    entries_.erase(iterator);
    return ttl;
}

std::optional<std::uint8_t> TtlTracker::compute_auto_ttl(
    std::uint8_t ttl,
    std::uint8_t first,
    std::uint8_t second,
    std::uint8_t min_hops,
    std::uint8_t max_ttl)
{
    int hops = 0;
    if (ttl > 98 && ttl < 128) {
        hops = 128 - ttl;
    }
    else if (ttl > 34 && ttl < 64) {
        hops = 64 - ttl;
    }
    else {
        return std::nullopt;
    }

    if (hops <= first || hops < min_hops) {
        return std::nullopt;
    }

    int fake_ttl = hops - second;
    if (fake_ttl < second && hops <= 9) {
        fake_ttl = hops - first -
                   static_cast<int>((second - first) *
                                    (static_cast<float>(hops) / 10.0F));
    }

    if (fake_ttl <= 0) {
        return std::nullopt;
    }
    if (max_ttl != 0 && fake_ttl > max_ttl) {
        fake_ttl = max_ttl;
    }
    return static_cast<std::uint8_t>(
        std::min(fake_ttl, static_cast<int>(0xFF)));
}

void TtlTracker::cleanup()
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

    // Bounded incremental cleanup with bucket rotation, so the packet thread
    // never walks the whole map at once and every entry is eventually visited.
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
