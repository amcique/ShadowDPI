#include "shadowdpi/cli.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shadowdpi/fake_payloads.hpp"
#include "shadowdpi/types.hpp"
#include "shadowdpi/util.hpp"
#include "shadowdpi/version.hpp"

namespace sdp {
namespace {

enum class ArgumentKind {
    none,
    required,
    optional,
};

[[nodiscard]] ArgumentKind long_argument_kind(std::string_view name)
{
    if (name == "help" || name == "version" || name == "verbose" ||
        name == "block-passive" || name == "passive-dpi" ||
        name == "block-quic" || name == "http-keepalive-nowait" ||
        name == "http-host-replace" || name == "http-host-no-space" ||
        name == "http-extra-space" || name == "http-host-mixed-case" ||
        name == "http-all-ports" || name == "dns-verb" ||
        name == "allow-no-sni" || name == "frag-by-sni" ||
        name == "fake-wrong-checksum" || name == "wrong-chksum" ||
        name == "fake-wrong-seq" || name == "wrong-seq" ||
        name == "native-frag" || name == "reverse-frag" ||
        name == "debug-exit") {
        return ArgumentKind::none;
    }

    if (name == "auto-ttl" || name == "max-payload") {
        return ArgumentKind::optional;
    }

    if (name == "preset" || name == "http-fragment" ||
        name == "http-keepalive-fragment" || name == "https-fragment" ||
        name == "port" || name == "ip-id" || name == "dns-addr" ||
        name == "dns-port" || name == "dnsv6-addr" || name == "dnsv6-port" ||
        name == "blacklist" || name == "ready-file" ||
        name == "fake-ttl" || name == "set-ttl" ||
        name == "min-ttl" || name == "fake-hex" || name == "fake-from-hex" ||
        name == "fake-sni" || name == "fake-with-sni" || name == "fake-gen" ||
        name == "fake-resend") {
        return ArgumentKind::required;
    }

    throw CliError("unknown option: --" + std::string(name));
}

[[nodiscard]] unsigned long parse_unsigned(std::string_view text,
                                           std::string_view option,
                                           unsigned long maximum)
{
    if (text.empty()) {
        throw CliError("missing value for " + std::string(option));
    }

    unsigned long value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [position, error] = std::from_chars(begin, end, value, 10);
    if (error != std::errc{} || position != end || value > maximum) {
        throw CliError("invalid value for " + std::string(option) + ": " +
                       std::string(text));
    }
    return value;
}

[[nodiscard]] std::uint16_t parse_u16(std::string_view text,
                                      std::string_view option)
{
    return static_cast<std::uint16_t>(parse_unsigned(text, option, 0xFFFF));
}

[[nodiscard]] std::uint8_t parse_u8(std::string_view text,
                                    std::string_view option)
{
    return static_cast<std::uint8_t>(parse_unsigned(text, option, 0xFF));
}

void add_unique(std::vector<std::uint16_t>& values, std::uint16_t value)
{
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void enable_native(Config& config)
{
    config.native_frag = true;
    config.http.persistent = true;
}

void parse_auto_ttl(Config& config, std::optional<std::string_view> value)
{
    config.fake.enabled = true;
    config.fake.fixed_ttl.reset();
    config.fake.auto_ttl.enabled = true;

    if (!value) {
        return;
    }

    if (value->find('-') == std::string_view::npos) {
        const auto second = parse_u8(*value, "--auto-ttl");
        config.fake.auto_ttl.first = second;
        config.fake.auto_ttl.second = second;
        return;
    }

    std::vector<std::string_view> parts;
    std::string_view rest = *value;
    while (true) {
        const auto separator = rest.find('-');
        parts.push_back(rest.substr(0, separator));
        if (separator == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(separator + 1);
    }

    if (parts.size() != 3) {
        throw CliError("--auto-ttl expects first-second-max or a single value");
    }

    config.fake.auto_ttl.first = parse_u8(parts[0], "--auto-ttl");
    config.fake.auto_ttl.second = parse_u8(parts[1], "--auto-ttl");
    config.fake.auto_ttl.max = parse_u8(parts[2], "--auto-ttl");
}

/**
 * Applies one long option to config.
 *
 * @param value Empty for flags, and set for options that accept an argument,
 *              either inline after '=' or as the next argv entry.
 */
void apply_long_option(Config& config,
                       std::string_view name,
                       std::optional<std::string_view> value)
{
    if (name == "help") {
        config.help = true;
    }
    else if (name == "version") {
        config.version = true;
    }
    else if (name == "verbose" || name == "dns-verb") {
        config.verbose = true;
    }
    else if (name == "preset") {
        apply_preset(config, static_cast<int>(parse_unsigned(*value, "--preset", 9)));
    }
    else if (name == "block-passive" || name == "passive-dpi") {
        config.block_passive = true;
    }
    else if (name == "block-quic") {
        config.block_quic = true;
    }
    else if (name == "http-fragment") {
        config.http.fragment_enabled = true;
        config.http.fragment_size = parse_u16(*value, "--http-fragment");
    }
    else if (name == "http-keepalive-fragment") {
        config.http.fragment_enabled = true;
        config.http.fragment_size = parse_u16(*value, "--http-keepalive-fragment");
        config.http.persistent = true;
        config.native_frag = true;
    }
    else if (name == "http-keepalive-nowait") {
        // Compatibility alias: ShadowDPI processes keep-alive requests
        // immediately and never waits for the first ACK.
        config.http.persistent = true;
        config.native_frag = true;
    }
    else if (name == "https-fragment") {
        config.https.fragment_enabled = true;
        config.https.fragment_size = parse_u16(*value, "--https-fragment");
    }
    else if (name == "http-host-replace") {
        config.http.replace_host = true;
    }
    else if (name == "http-host-no-space") {
        config.http.remove_host_space = true;
    }
    else if (name == "http-extra-space") {
        config.http.extra_space = true;
        config.http.remove_host_space = true;
    }
    else if (name == "http-host-mixed-case") {
        config.http.mixed_case_host = true;
    }
    else if (name == "http-all-ports") {
        config.http.all_ports = true;
    }
    else if (name == "port") {
        const auto port = parse_u16(*value, "--port");
        if (port == 0) {
            throw CliError("--port must be in the range 1-65535");
        }
        add_unique(config.extra_ports, port);
    }
    else if (name == "ip-id") {
        add_unique(config.ip_ids, parse_u16(*value, "--ip-id"));
    }
    else if (name == "dns-addr") {
        const auto address = parse_ip_address(*value);
        if (!address || address->family != IpFamily::v4) {
            throw CliError("invalid IPv4 address for --dns-addr");
        }
        if (config.dns.v4_server) {
            throw CliError("--dns-addr may only be specified once");
        }
        config.dns.v4_server = *address;
    }
    else if (name == "dnsv6-addr") {
        const auto address = parse_ip_address(*value);
        if (!address || address->family != IpFamily::v6) {
            throw CliError("invalid IPv6 address for --dnsv6-addr");
        }
        if (config.dns.v6_server) {
            throw CliError("--dnsv6-addr may only be specified once");
        }
        config.dns.v6_server = *address;
    }
    else if (name == "dns-port") {
        config.dns.v4_port = parse_u16(*value, "--dns-port");
    }
    else if (name == "dnsv6-port") {
        config.dns.v6_port = parse_u16(*value, "--dnsv6-port");
    }
    else if (name == "blacklist") {
        config.blacklists.emplace_back(std::string(*value));
    }
    else if (name == "ready-file") {
        if (value->empty()) {
            throw CliError("--ready-file requires a path");
        }
        if (!config.ready_file.empty()) {
            throw CliError("--ready-file may only be specified once");
        }
        config.ready_file = std::string(*value);
    }
    else if (name == "allow-no-sni") {
        config.allow_no_sni = true;
    }
    else if (name == "frag-by-sni") {
        config.https.by_sni = true;
    }
    else if (name == "fake-ttl" || name == "set-ttl") {
        config.fake.enabled = true;
        config.fake.auto_ttl.enabled = false;
        config.fake.fixed_ttl = parse_u8(*value, "--fake-ttl");
    }
    else if (name == "auto-ttl") {
        parse_auto_ttl(config, value);
    }
    else if (name == "min-ttl") {
        config.fake.enabled = true;
        config.fake.min_ttl = parse_u8(*value, "--min-ttl");
    }
    else if (name == "fake-wrong-checksum" || name == "wrong-chksum") {
        config.fake.enabled = true;
        config.fake.wrong_checksum = true;
    }
    else if (name == "fake-wrong-seq" || name == "wrong-seq") {
        config.fake.enabled = true;
        config.fake.wrong_seq = true;
    }
    else if (name == "native-frag") {
        enable_native(config);
    }
    else if (name == "reverse-frag") {
        config.reverse_frag = true;
        enable_native(config);
    }
    else if (name == "max-payload") {
        config.max_payload = value ? parse_u16(*value, "--max-payload") : 1200;
    }
    else if (name == "fake-hex" || name == "fake-from-hex") {
        auto payload = parse_hex_payload(*value);
        if (!payload) {
            throw CliError("invalid hexadecimal fake payload");
        }
        if (config.fake.payloads.size() >= kMaxFakePackets) {
            throw CliError("too many fake payloads (maximum is 30)");
        }
        config.fake.payloads.push_back(std::move(*payload));
    }
    else if (name == "fake-sni" || name == "fake-with-sni") {
        if (value->empty() || value->size() > kMaxHostLength) {
            throw CliError("invalid domain name for --fake-sni");
        }
        if (config.fake.payloads.size() >= kMaxFakePackets) {
            throw CliError("too many fake payloads (maximum is 30)");
        }
        config.fake.payloads.push_back(make_fake_client_hello(*value));
    }
    else if (name == "fake-gen") {
        const auto count = parse_unsigned(*value, "--fake-gen", kMaxFakePackets);
        if (count == 0) {
            throw CliError("--fake-gen requires at least one packet");
        }
        auto generated = make_random_fake_payloads(
            static_cast<unsigned int>(count), 200);
        if (config.fake.payloads.size() + generated.size() > kMaxFakePackets) {
            throw CliError("too many fake payloads (maximum is 30)");
        }
        for (auto& payload : generated) {
            config.fake.payloads.push_back(std::move(payload));
        }
    }
    else if (name == "fake-resend") {
        config.fake.resend = parse_u8(*value, "--fake-resend");
    }
    else if (name == "debug-exit") {
        config.debug_exit = true;
    }
}

std::string_view require_short_value(std::string_view cluster,
                                     std::size_t& index,
                                     int& argument_index,
                                     int argc,
                                     char* argv[],
                                     std::string_view option)
{
    if (index + 1 < cluster.size()) {
        const auto value = cluster.substr(index + 1);
        index = cluster.size();
        return value;
    }

    ++argument_index;
    if (argument_index >= argc) {
        throw CliError("missing value for " + std::string(option));
    }
    index = cluster.size();
    return argv[argument_index];
}

void apply_short_option(Config& config, char option)
{
    switch (option) {
    case 'p':
        config.block_passive = true;
        break;
    case 'q':
        config.block_quic = true;
        break;
    case 'r':
        config.http.replace_host = true;
        break;
    case 's':
        config.http.remove_host_space = true;
        break;
    case 'a':
        config.http.extra_space = true;
        config.http.remove_host_space = true;
        break;
    case 'm':
        config.http.mixed_case_host = true;
        break;
    case 'w':
        config.http.all_ports = true;
        break;
    case 'n':
        config.http.persistent = true;
        config.native_frag = true;
        break;
    case 'v':
        config.verbose = true;
        break;
    case 'h':
        config.help = true;
        break;
    case 'V':
        config.version = true;
        break;
    default:
        throw CliError(std::string("unknown option: -") + option);
    }
}

} // namespace

/**
 * Parses argv into a Config.
 *
 * Presets are applied in the order they appear, so a later flag can override an
 * earlier preset. Options that take a separate value consume the next argument,
 * which is why the index advances by hand.
 *
 * @param argc Argument count, as passed to main.
 * @param argv Argument vector, as passed to main.
 * @return The parsed configuration, already finalized unless help or version
 *         was requested.
 */
Config parse_cli(int argc, char* argv[])
{
    Config config;

    if (argc <= 1) {
        apply_preset(config, 9);
        finalize_config(config);
        return config;
    }

    int argument_index = 1;
    while (argument_index < argc) {
        const std::string_view argument = argv[argument_index];
        if (argument == "--") {
            if (argument_index + 1 < argc) {
                throw CliError("unexpected positional argument: " +
                               std::string(argv[argument_index + 1]));
            }
            break;
        }

        if (argument.starts_with("--")) {
            const auto equals = argument.find('=');
            const std::string_view name =
                equals == std::string_view::npos
                    ? argument.substr(2)
                    : argument.substr(2, equals - 2);

            std::optional<std::string_view> inline_value;
            if (equals != std::string_view::npos) {
                inline_value = argument.substr(equals + 1);
            }

            const auto kind = long_argument_kind(name);
            std::optional<std::string_view> value;
            if (kind == ArgumentKind::required) {
                if (inline_value) {
                    value = *inline_value;
                }
                else {
                    ++argument_index;
                    if (argument_index >= argc) {
                        throw CliError("missing value for --" + std::string(name));
                    }
                    value = argv[argument_index];
                }
            }
            else if (kind == ArgumentKind::optional) {
                if (inline_value) {
                    value = *inline_value;
                }
                else if (argument_index + 1 < argc &&
                         argv[argument_index + 1][0] != '-') {
                    value = argv[++argument_index];
                }
            }
            else if (inline_value) {
                throw CliError("--" + std::string(name) + " does not take a value");
            }

            apply_long_option(config, name, value);
            ++argument_index;
            continue;
        }

        if (argument.size() > 1 && argument.front() == '-') {
            const std::string_view cluster = argument.substr(1);
            for (std::size_t index = 0; index < cluster.size(); ++index) {
                const char option = cluster[index];
                if (option >= '1' && option <= '9') {
                    apply_preset(config, option - '0');
                    continue;
                }

                if (option == 'f') {
                    const auto value = require_short_value(
                        cluster, index, argument_index, argc, argv, "-f");
                    config.http.fragment_enabled = true;
                    config.http.fragment_size = parse_u16(value, "-f");
                }
                else if (option == 'k') {
                    const auto value = require_short_value(
                        cluster, index, argument_index, argc, argv, "-k");
                    config.http.fragment_enabled = true;
                    config.http.fragment_size = parse_u16(value, "-k");
                    config.http.persistent = true;
                    config.native_frag = true;
                }
                else if (option == 'e') {
                    const auto value = require_short_value(
                        cluster, index, argument_index, argc, argv, "-e");
                    config.https.fragment_enabled = true;
                    config.https.fragment_size = parse_u16(value, "-e");
                }
                else {
                    apply_short_option(config, option);
                }
            }
            ++argument_index;
            continue;
        }

        throw CliError("unexpected positional argument: " + std::string(argument));
    }

    if (config.help || config.version) {
        return config;
    }

    finalize_config(config);
    return config;
}

std::string version_text()
{
    return std::string(kProgramName) + " " + kVersion + " (C++20)\n";
}

std::string help_text()
{
    return
        "Usage: shadowdpi.exe [OPTION...]\n"
        "\n"
        "General:\n"
        "  -h, --help                     show this help\n"
        "  -V, --version                  show version information\n"
        "  -v, --verbose                  verbose diagnostics\n"
        "  --preset <1-9>                 apply a preset mode (also -1 ... -9)\n"
        "  --debug-exit                   open filters and exit immediately\n"
        "  --ready-file <path>            write a file after the filters open\n"
        "\n"
        "Passive DPI and QUIC:\n"
        "  -p, --block-passive            block passive DPI redirects/RSTs\n"
        "  -q, --block-quic               block QUIC/HTTP3 Initial packets\n"
        "\n"
        "HTTP manipulation:\n"
        "  --http-fragment <size>         fragment HTTP requests (-f <size>)\n"
        "  --http-keepalive-fragment <n>  fragment persistent HTTP (-k <n>)\n"
        "  --http-keepalive-nowait        handle keep-alive HTTP immediately (-n)\n"
        "  -r, --http-host-replace        replace Host with hoSt\n"
        "  -s, --http-host-no-space       remove the space after Host:\n"
        "  -a, --http-extra-space         add space between method and URI\n"
        "  -m, --http-host-mixed-case     mix the case of the Host value\n"
        "  -w, --http-all-ports           parse HTTP on all processed ports\n"
        "\n"
        "HTTPS manipulation:\n"
        "  -e, --https-fragment <size>    fragment TLS ClientHello\n"
        "  --frag-by-sni                  fragment immediately before SNI\n"
        "\n"
        "Fragmentation:\n"
        "  --native-frag                  split packets into smaller TCP segments\n"
        "  --reverse-frag                 send split segments in reverse order\n"
        "  --max-payload [size]           skip large TCP payloads (default 1200)\n"
        "\n"
        "Fake Request Mode:\n"
        "  --fake-ttl <ttl>               send fake requests with a fixed TTL\n"
        "  --auto-ttl [a1-a2-max]         derive fake TTL from observed hops\n"
        "  --min-ttl <hops>               minimum hop distance for fake requests\n"
        "  --fake-wrong-checksum          send fake requests with a bad checksum\n"
        "  --fake-wrong-seq               send fake requests with old SEQ/ACK\n"
        "  --fake-hex <hex>               add a fake payload from hex bytes\n"
        "  --fake-sni <domain>            add a generated ClientHello payload\n"
        "  --fake-gen <count>             add random fake payloads\n"
        "  --fake-resend <count>          resend each fake payload N times\n"
        "\n"
        "Host selection:\n"
        "  --blacklist <file>             only apply tricks to listed domains\n"
        "                                 (may be supplied multiple times)\n"
        "  --allow-no-sni                 apply HTTPS tricks when SNI is missing\n"
        "\n"
        "Networking:\n"
        "  --port <port>                  process an additional TCP port\n"
        "  --ip-id <id>                   handle an additional spoofed IP ID\n"
        "  --dns-addr <ipv4>              redirect IPv4 DNS requests\n"
        "  --dns-port <port>              redirected IPv4 DNS port (53 default)\n"
        "  --dnsv6-addr <ipv6>            redirect IPv6 DNS requests\n"
        "  --dnsv6-port <port>            redirected IPv6 DNS port (53 default)\n"
        "\n"
        "Presets:\n"
        "  -1  -p -r -s -f 2 -k 2 -n -e 2\n"
        "  -2  -p -r -s -f 2 -k 2 -n -e 40\n"
        "  -3  -p -r -s -e 40\n"
        "  -4  -p -r -s\n"
        "  -5  -f 2 -e 2 --auto-ttl --reverse-frag --max-payload\n"
        "  -6  -f 2 -e 2 --wrong-seq --reverse-frag --max-payload\n"
        "  -7  -f 2 -e 2 --wrong-chksum --reverse-frag --max-payload\n"
        "  -8  -f 2 -e 2 --wrong-seq --wrong-chksum --reverse-frag --max-payload\n"
        "  -9  -8 plus -q (default when no options are supplied)\n";
}

} // namespace sdp
