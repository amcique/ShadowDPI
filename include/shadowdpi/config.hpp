#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "shadowdpi/error.hpp"
#include "shadowdpi/types.hpp"

namespace sdp {

struct AutoTtlOptions {
    bool enabled{false};
    std::uint8_t first{1};
    std::uint8_t second{4};
    std::uint8_t max{10};
};

struct HttpOptions {
    bool fragment_enabled{false};
    std::uint16_t fragment_size{0};
    bool persistent{false};
    bool replace_host{false};
    bool remove_host_space{false};
    bool extra_space{false};
    bool mixed_case_host{false};
    bool all_ports{false};
};

struct HttpsOptions {
    bool fragment_enabled{false};
    std::uint16_t fragment_size{0};
    bool by_sni{false};
};

struct FakeOptions {
    bool enabled{false};
    std::optional<std::uint8_t> fixed_ttl;
    AutoTtlOptions auto_ttl;
    std::uint8_t min_ttl{0};
    bool wrong_checksum{false};
    bool wrong_seq{false};
    std::vector<std::vector<std::byte>> payloads;
    std::uint8_t resend{1};
};

struct DnsOptions {
    std::optional<IpAddress> v4_server;
    std::optional<IpAddress> v6_server;
    std::uint16_t v4_port{53};
    std::uint16_t v6_port{53};
};

struct Config {
    bool block_passive{false};
    bool block_quic{false};

    HttpOptions http;
    HttpsOptions https;

    bool native_frag{false};
    bool reverse_frag{false};
    std::uint16_t max_payload{0};

    std::vector<std::uint16_t> extra_ports;
    std::vector<std::uint16_t> ip_ids;
    std::vector<std::filesystem::path> blacklists;
    std::filesystem::path ready_file;
    bool allow_no_sni{false};

    FakeOptions fake;
    DnsOptions dns;

    bool verbose{false};
    bool debug_exit{false};
    bool help{false};
    bool version{false};
};

/// Applies one of the presets (1-9) as a group of additive options.
void apply_preset(Config& config, int preset);

/// Fills in defaults and validates cross-option constraints.
/// Throws std::invalid_argument with a user-facing message on invalid configuration.
void finalize_config(Config& config);

} // namespace sdp
