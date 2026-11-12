#include "shadowdpi/dns_tracker.hpp"

#include <functional>

#include "shadowdpi/util.hpp"

namespace sdp {
namespace {

void hash_combine(std::size_t& seed, std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
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

    const Key key{ipv6, local_ip, local_port};
    auto [iterator, inserted] = entries_.try_emplace(
        key, Entry{remote_ip, remote_port, Clock::now()});
    if (!inserted) {
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

    const Key key{ipv6, local_ip, local_port};
    const auto iterator = entries_.find(key);
    if (iterator == entries_.end()) {
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

    for (auto iterator = entries_.begin(); iterator != entries_.end();) {
        if (now - iterator->second.timestamp >= kEntryLifetime) {
            iterator = entries_.erase(iterator);
        }
        else {
            ++iterator;
        }
    }
}

} // namespace sdp
