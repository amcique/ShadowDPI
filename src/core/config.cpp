#include "shadowdpi/config.hpp"

#include <stdexcept>

namespace sdp {
namespace {

void enable_http_fragment(Config& config, std::uint16_t size)
{
    config.http.fragment_enabled = true;
    config.http.fragment_size = size;
}

void enable_https_fragment(Config& config, std::uint16_t size)
{
    config.https.fragment_enabled = true;
    config.https.fragment_size = size;
}

void enable_native_fragmentation(Config& config)
{
    config.native_frag = true;
    config.http.persistent = true;
    config.http.persistent_nowait = true;
}

} // namespace

void apply_preset(Config& config, int preset)
{
    switch (preset) {
    case 1:
    case 2:
    case 3:
    case 4:
        config.block_passive = true;
        config.http.replace_host = true;
        config.http.remove_host_space = true;
        if (preset <= 2) {
            enable_http_fragment(config, 2);
            enable_https_fragment(config, preset == 1 ? 2 : 40);
            config.http.persistent = true;
            config.http.persistent_nowait = true;
        }
        else if (preset == 3) {
            enable_https_fragment(config, 40);
        }
        break;

    case 5:
        enable_http_fragment(config, 2);
        enable_https_fragment(config, 2);
        enable_native_fragmentation(config);
        config.reverse_frag = true;
        config.fake.enabled = true;
        config.fake.auto_ttl.enabled = true;
        config.max_payload = 1200;
        break;

    case 6:
        enable_http_fragment(config, 2);
        enable_https_fragment(config, 2);
        enable_native_fragmentation(config);
        config.reverse_frag = true;
        config.fake.enabled = true;
        config.fake.wrong_seq = true;
        config.max_payload = 1200;
        break;

    case 7:
    case 8:
    case 9:
        enable_http_fragment(config, 2);
        enable_https_fragment(config, 2);
        enable_native_fragmentation(config);
        config.reverse_frag = true;
        config.fake.enabled = true;
        config.fake.wrong_checksum = true;
        config.max_payload = 1200;
        if (preset >= 8) {
            config.fake.wrong_seq = true;
        }
        if (preset == 9) {
            config.block_quic = true;
        }
        break;

    default:
        throw std::invalid_argument("preset must be in the range 1-9");
    }
}

void finalize_config(Config& config)
{
    if (config.http.fragment_enabled && config.http.fragment_size == 0) {
        config.http.fragment_size = 2;
    }
    if (config.https.fragment_enabled && config.https.fragment_size == 0) {
        config.https.fragment_size = 2;
    }

    if (config.https.by_sni) {
        config.https.fragment_enabled = true;
        if (config.https.fragment_size == 0) {
            config.https.fragment_size = 2;
        }
        config.native_frag = true;
    }

    if (config.reverse_frag) {
        config.native_frag = true;
    }

    if (config.native_frag && !config.http.fragment_enabled &&
        !config.https.fragment_enabled) {
        throw std::invalid_argument(
            "native fragmentation is enabled but no HTTP or HTTPS "
            "fragmentation size is set");
    }

    if (config.fake.auto_ttl.enabled) {
        if (config.fake.auto_ttl.first == 0) {
            config.fake.auto_ttl.first = 1;
        }
        if (config.fake.auto_ttl.second == 0) {
            config.fake.auto_ttl.second = 4;
        }
        if (config.fake.auto_ttl.max == 0) {
            config.fake.auto_ttl.max = 10;
        }
        if (config.fake.min_ttl == 0) {
            config.fake.min_ttl = 3;
        }
    }

    if (config.dns.v4_server && config.dns.v4_port == 0) {
        throw std::invalid_argument("DNS IPv4 port must be greater than zero");
    }
    if (config.dns.v6_server && config.dns.v6_port == 0) {
        throw std::invalid_argument("DNS IPv6 port must be greater than zero");
    }
    if (!config.dns.v4_server && config.dns.v4_port != 53) {
        throw std::invalid_argument("--dns-port requires --dns-addr");
    }
    if (!config.dns.v6_server && config.dns.v6_port != 53) {
        throw std::invalid_argument("--dnsv6-port requires --dnsv6-addr");
    }
}

} // namespace sdp
