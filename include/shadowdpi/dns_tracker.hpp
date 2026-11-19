#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

namespace sdp {

struct DnsRedirect {
    std::array<std::uint32_t, 4> remote_ip{};
    std::uint16_t remote_port{0};
};

class DnsTracker {
public:
    /// Upper bound on tracked queries. A flood must not grow the map without
    /// limit; the oldest-ish entry is evicted when the cap is reached.
    static constexpr std::size_t kMaxEntries{8192};

    /// Records an outgoing DNS request. Returns true when the packet is a DNS
    /// request and should be redirected.
    bool handle_outgoing(const std::array<std::uint32_t, 4>& local_ip,
                         std::uint16_t local_port,
                         const std::array<std::uint32_t, 4>& remote_ip,
                         std::uint16_t remote_port,
                         std::span<const std::byte> payload,
                         bool ipv6);

    /// Looks up an incoming DNS response and returns the original resolver
    /// endpoint. The entry is consumed only when the local endpoint,
    /// transaction ID and question match; a mismatch leaves it in place.
    [[nodiscard]] std::optional<DnsRedirect> handle_incoming(
        const std::array<std::uint32_t, 4>& local_ip,
        std::uint16_t local_port,
        std::span<const std::byte> payload,
        bool ipv6);

    [[nodiscard]] static bool is_dns_packet(
        std::span<const std::byte> payload,
        bool outgoing);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Key {
        bool ipv6{false};
        std::array<std::uint32_t, 4> ip{};
        std::uint16_t port{0};
        std::uint16_t transaction_id{0};
        std::string question;

        friend bool operator==(const Key&, const Key&) = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept;
    };

    struct Entry {
        std::array<std::uint32_t, 4> remote_ip{};
        std::uint16_t remote_port{0};
        std::chrono::steady_clock::time_point timestamp;
    };

    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds kCleanupInterval{1};
    static constexpr std::chrono::seconds kEntryLifetime{30};
    static constexpr std::size_t kCleanupBatch{512};

    void cleanup();

    std::unordered_map<Key, Entry, KeyHash> entries_;
    std::optional<Clock::time_point> last_cleanup_;
    std::size_t next_bucket_{0};
};

} // namespace sdp
