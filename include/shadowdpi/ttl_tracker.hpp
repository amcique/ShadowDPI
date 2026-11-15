#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace sdp {

class TtlTracker {
public:
    void record_incoming(const std::array<std::uint32_t, 4>& remote_ip,
                         const std::array<std::uint32_t, 4>& local_ip,
                         std::uint16_t remote_port,
                         std::uint16_t local_port,
                         bool ipv6,
                         std::uint8_t ttl);

    /// Looks up and consumes the TTL recorded for the reverse direction.
    [[nodiscard]] std::optional<std::uint8_t> lookup_outgoing(
        const std::array<std::uint32_t, 4>& local_ip,
        const std::array<std::uint32_t, 4>& remote_ip,
        std::uint16_t local_port,
        std::uint16_t remote_port,
        bool ipv6);

    /// Computes the fake-packet TTL from the observed TTL and hop distance.
    [[nodiscard]] static std::optional<std::uint8_t> compute_auto_ttl(
        std::uint8_t ttl,
        std::uint8_t first,
        std::uint8_t second,
        std::uint8_t min_hops,
        std::uint8_t max_ttl);

    /// Upper bound on tracked connections. SYN/ACK floods must not grow the
    /// map without limit.
    static constexpr std::size_t kMaxEntries{8192};

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Endpoint {
        std::array<std::uint32_t, 4> ip{};
        std::uint16_t port{0};

        friend bool operator==(const Endpoint&, const Endpoint&) = default;
    };

    struct Key {
        bool ipv6{false};
        Endpoint source;
        Endpoint destination;

        friend bool operator==(const Key&, const Key&) = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept;
    };

    struct Entry {
        std::uint8_t ttl{0};
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
