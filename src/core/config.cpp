#include "shadowdpi/config.hpp"

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
            config.native_frag = true;
            config.http.persistent = true;
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
        throw CliError("preset must be in the range 1-9");
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

    // The engine only splits when *_fragment_enabled is set. Standalone
    // native-frag flags get the preset default; explicit sizes are kept.
    if (config.native_frag && !config.http.fragment_enabled &&
        !config.https.fragment_enabled) {
        config.http.fragment_enabled = true;
        config.http.fragment_size = 2;
        config.https.fragment_enabled = true;
        config.https.fragment_size = 2;
    }

    // Check after parsing: later options can reset the TTL source.
    // --min-ttl is a gate, not a TTL source.
    if (config.fake.enabled && config.fake.resend != 0 &&
        !config.fake.fixed_ttl.value_or(0) &&
        !config.fake.auto_ttl.enabled &&
        !config.fake.wrong_checksum && !config.fake.wrong_seq) {
        throw CliError(
            "fake request mode needs a neutralizer: --fake-ttl, --auto-ttl, "
            "--fake-wrong-seq, or --fake-wrong-checksum");
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
        if (config.fake.auto_ttl.second < config.fake.auto_ttl.first) {
            throw CliError("--auto-ttl second value must not be less than first");
        }
        if (config.fake.auto_ttl.max < config.fake.auto_ttl.first) {
            throw CliError("--auto-ttl max must be at least the first value");
        }
        // min_ttl is uint8_t, so its range is already bounded to 0..255.
    }

    if (config.dns.v4_server && config.dns.v4_port == 0) {
        throw CliError("DNS IPv4 port must be greater than zero");
    }
    if (config.dns.v6_server && config.dns.v6_port == 0) {
        throw CliError("DNS IPv6 port must be greater than zero");
    }
    if (!config.dns.v4_server && config.dns.v4_port != 53) {
        throw CliError("--dns-port requires --dns-addr");
    }
    if (!config.dns.v6_server && config.dns.v6_port != 53) {
        throw CliError("--dnsv6-port requires --dnsv6-addr");
    }
}

} // namespace sdp
