#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "shadowdpi/blacklist.hpp"
#include "shadowdpi/cli.hpp"
#include "shadowdpi/config.hpp"
#include "shadowdpi/dns_tracker.hpp"
#include "shadowdpi/fake_payloads.hpp"
#include "shadowdpi/filter.hpp"
#include "shadowdpi/http.hpp"
#include "shadowdpi/tls.hpp"
#include "shadowdpi/ttl_tracker.hpp"
#include "shadowdpi/types.hpp"
#include "shadowdpi/util.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, std::string_view expression, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

[[nodiscard]] std::span<const std::byte> as_bytes(std::string_view text)
{
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

[[nodiscard]] std::vector<std::byte> to_buffer(std::string_view text)
{
    std::vector<std::byte> buffer(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        buffer[index] = static_cast<std::byte>(
            static_cast<unsigned char>(text[index]));
    }
    return buffer;
}

[[nodiscard]] std::string from_buffer(std::span<const std::byte> buffer)
{
    std::string result;
    result.reserve(buffer.size());
    for (const auto byte : buffer) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> make_dns_message(bool response,
                                                      std::uint16_t id)
{
    std::vector<std::byte> message(12, std::byte{0});
    message[0] = static_cast<std::byte>(id >> 8U);
    message[1] = static_cast<std::byte>(id & 0xFFU);
    message[2] = response ? std::byte{0x81} : std::byte{0x01};
    message[3] = response ? std::byte{0x80} : std::byte{0x00};
    message[5] = std::byte{0x01}; // QDCOUNT

    for (const auto part : {std::string_view{"example"},
                            std::string_view{"com"}}) {
        message.push_back(static_cast<std::byte>(part.size()));
        for (const char character : part) {
            message.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
    }
    message.push_back(std::byte{0}); // end of QNAME
    message.push_back(std::byte{0});
    message.push_back(std::byte{1}); // QTYPE A
    message.push_back(std::byte{0});
    message.push_back(std::byte{1}); // QCLASS IN
    return message;
}

[[nodiscard]] sdp::Config parse(std::initializer_list<std::string_view> arguments)
{
    std::vector<std::string> storage;
    storage.emplace_back("shadowdpi.exe");
    for (const auto argument : arguments) {
        storage.emplace_back(argument);
    }

    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& argument : storage) {
        argv.push_back(argument.data());
    }
    return sdp::parse_cli(static_cast<int>(argv.size()), argv.data());
}

[[nodiscard]] sdp::Config parse(
    const std::vector<std::string_view>& arguments)
{
    std::vector<std::string> storage;
    storage.emplace_back("shadowdpi.exe");
    for (const auto& argument : arguments) {
        storage.emplace_back(argument);
    }
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& argument : storage) {
        argv.push_back(argument.data());
    }
    return sdp::parse_cli(static_cast<int>(argv.size()), argv.data());
}

[[nodiscard]] bool parse_throws(
    std::initializer_list<std::string_view> arguments)
{
    try {
        (void)parse(arguments);
        return false;
    }
    catch (const std::exception&) {
        return true;
    }
}

template <typename Exception>
[[nodiscard]] bool parse_throws_as(
    std::initializer_list<std::string_view> arguments)
{
    try {
        (void)parse(arguments);
    }
    catch (const Exception&) {
        return true;
    }
    catch (...) {
        return false;
    }
    return false;
}

template <typename Exception>
[[nodiscard]] bool parse_throws_as(
    const std::vector<std::string_view>& arguments)
{
    try {
        (void)parse(arguments);
    }
    catch (const Exception&) {
        return true;
    }
    catch (...) {
        return false;
    }
    return false;
}

void test_config_defaults()
{
    const sdp::Config config;
    CHECK(!config.block_passive);
    CHECK(!config.block_quic);
    CHECK(!config.native_frag);
    CHECK(!config.http.fragment_enabled);
    CHECK(!config.https.fragment_enabled);
    CHECK(!config.fake.enabled);
    CHECK(config.http.fragment_size == 0);
    CHECK(config.https.fragment_size == 0);
    CHECK(config.max_payload == 0);
}

void test_preset9_defaults()
{
    const auto config = parse({});
    CHECK(config.block_quic);
    CHECK(config.fake.enabled);
    CHECK(config.fake.wrong_checksum);
    CHECK(config.fake.wrong_seq);
    CHECK(config.reverse_frag);
    CHECK(config.native_frag);
    CHECK(config.max_payload == 1200);
    CHECK(config.http.fragment_enabled);
    CHECK(config.http.fragment_size == 2);
    CHECK(config.https.fragment_enabled);
    CHECK(config.https.fragment_size == 2);
}

void test_native_frag_defaults()
{
    // Standalone split flags must split both HTTP and HTTPS.
    for (const auto* argument : {"-n", "--native-frag", "--reverse-frag",
                                 "--http-keepalive-nowait"}) {
        const auto config = parse({argument});
        CHECK(config.native_frag);
        CHECK(config.http.fragment_enabled);
        CHECK(config.https.fragment_enabled);
        CHECK(config.http.fragment_size == 2);
        CHECK(config.https.fragment_size == 2);
    }

    // Explicit sizes are never overwritten by the default.
    const auto explicit_sizes = parse({"-n", "-f", "7", "-e", "9"});
    CHECK(explicit_sizes.http.fragment_size == 7);
    CHECK(explicit_sizes.https.fragment_size == 9);
}

void test_fake_mode_validation()
{
    // Custom payloads need a neutralizer.
    CHECK(parse_throws_as<sdp::CliError>({"--fake-sni", "example.com"}));
    CHECK(parse_throws_as<sdp::CliError>({"--fake-hex", "16030100"}));
    CHECK(parse_throws_as<sdp::CliError>({"--fake-gen", "2"}));
    CHECK(parse_throws_as<sdp::CliError>({"--min-ttl", "5"}));
    CHECK(parse_throws_as<sdp::CliError>({"--min-ttl", "0"}));
    CHECK(parse_throws_as<sdp::CliError>({"--fake-ttl", "0"}));
    CHECK(parse_throws_as<sdp::CliError>({"--set-ttl", "0"}));

    // Each neutralizer works in either option order.
    const std::vector<std::vector<std::string_view>> neutralizers{
        {"--fake-ttl", "5"},
        {"--auto-ttl"},
        {"--fake-wrong-seq"},
        {"--fake-wrong-checksum"},
    };
    for (const auto& neutralizer : neutralizers) {
        std::vector<std::string_view> after{"--fake-sni", "example.com"};
        after.insert(after.end(), neutralizer.begin(), neutralizer.end());
        const auto first = parse(after);
        CHECK(first.fake.enabled);
        CHECK(first.fake.payloads.size() == 1);

        std::vector<std::string_view> before = neutralizer;
        before.push_back("--fake-sni");
        before.push_back("example.com");
        const auto second = parse(before);
        CHECK(second.fake.enabled);
        CHECK(second.fake.payloads.size() == 1);
    }

    const auto with_value =
        parse({"--fake-sni", "example.com", "--fake-ttl", "5"});
    CHECK(with_value.fake.fixed_ttl.has_value());
    CHECK(*with_value.fake.fixed_ttl == 5);

    // --fake-resend 0 stays a documented no-op rather than being rejected.
    const auto no_resend =
        parse({"--fake-sni", "example.com", "--fake-ttl", "5",
               "--fake-resend", "0"});
    CHECK(no_resend.fake.resend == 0);

    // Domain contents are validated, not only the length.
    CHECK(parse_throws_as<sdp::CliError>({"--fake-sni", "bad host"}));
    CHECK(parse_throws_as<sdp::CliError>({"--fake-sni", "a..b.com"}));
    CHECK(parse_throws_as<sdp::CliError>({"--fake-sni", "-bad.com"}));
    CHECK(parse({"--fake-sni", "www.example.com.", "--fake-ttl", "5"})
              .fake.payloads.size() == 1);
}

void test_cli_options()
{
    auto config = parse({"-p", "-q", "-r", "-s", "-m", "-w",
                         "-f", "3", "-e", "40", "--port", "8080",
                         "--port", "8443", "--ip-id", "7",
                         "--max-payload", "900"});
    CHECK(config.block_passive);
    CHECK(config.block_quic);
    CHECK(config.http.replace_host);
    CHECK(config.http.remove_host_space);
    CHECK(config.http.mixed_case_host);
    CHECK(config.http.all_ports);
    CHECK(config.http.fragment_size == 3);
    CHECK(config.https.fragment_size == 40);
    CHECK(config.extra_ports.size() == 2);
    CHECK(config.extra_ports[0] == 8080);
    CHECK(config.extra_ports[1] == 8443);
    CHECK(config.ip_ids.size() == 1);
    CHECK(config.ip_ids[0] == 7);
    CHECK(config.max_payload == 900);

    config = parse({"--http-fragment=2", "--http-keepalive-fragment", "4",
                    "--https-fragment", "40", "--http-host-replace"});
    CHECK(config.http.fragment_enabled);
    CHECK(config.http.fragment_size == 4);
    CHECK(config.http.persistent);
    CHECK(config.native_frag);
    CHECK(config.https.fragment_size == 40);
    CHECK(config.http.replace_host);

    config = parse({"-f", "2", "-n"});
    CHECK(config.http.persistent);
    CHECK(config.native_frag);

    config = parse({"--dns-addr", "8.8.8.8", "--dns-port", "5353",
                    "--dnsv6-addr", "2001:4860:4860::8888",
                    "--dnsv6-port", "5353"});
    CHECK(config.dns.v4_server.has_value());
    CHECK(config.dns.v4_port == 5353);
    CHECK(config.dns.v6_server.has_value());
    CHECK(config.dns.v6_port == 5353);

    config = parse({"--ready-file", "C:/temp/ready.txt"});
    CHECK(config.ready_file ==
          std::filesystem::path("C:/temp/ready.txt"));

    config = parse({"--auto-ttl", "1-4-10", "--min-ttl", "3"});
    CHECK(config.fake.enabled);
    CHECK(config.fake.auto_ttl.enabled);
    CHECK(config.fake.auto_ttl.first == 1);
    CHECK(config.fake.auto_ttl.second == 4);
    CHECK(config.fake.auto_ttl.max == 10);
    CHECK(config.fake.min_ttl == 3);

    config = parse({"--auto-ttl", "6"});
    CHECK(config.fake.auto_ttl.first == 6);
    CHECK(config.fake.auto_ttl.second == 6);

    config = parse({"--fake-ttl", "5", "--fake-gen", "2",
                    "--fake-sni", "example.com", "--fake-resend", "3"});
    CHECK(config.fake.fixed_ttl.has_value());
    CHECK(*config.fake.fixed_ttl == 5);
    CHECK(config.fake.payloads.size() == 3);
    CHECK(config.fake.resend == 3);

    CHECK(parse_throws({"--port", "0"}));
    CHECK(parse_throws({"--port", "65536"}));
    CHECK(parse_throws({"--dns-port", "5353"}));
    CHECK(parse_throws({"--dns-addr", "not-an-ip"}));
    CHECK(parse_throws({"--dnsv6-addr", "1.2.3.4"}));
    CHECK(parse_throws({"--unknown-option"}));
    CHECK(parse_throws({"--ready-file", ""}));
    CHECK(parse_throws({"--ready-file", "a", "--ready-file", "b"}));
    CHECK(parse_throws({"--fake-gen", "0"}));
    CHECK(parse_throws({"--auto-ttl", "1-2"}));
    CHECK(parse_throws({"--preset", "10"}));

    // All CLI errors carry the usage hint.
    CHECK(parse_throws_as<sdp::CliError>({"--preset", "0"}));
    CHECK(parse_throws_as<sdp::CliError>({"-0"}));
    CHECK(parse_throws_as<sdp::CliError>(
        {"--dns-addr", "8.8.8.8", "--dns-port", "0"}));
    CHECK(parse_throws_as<sdp::CliError>(
        {"--dnsv6-addr", "2001:4860::8888", "--dnsv6-port", "0"}));
    CHECK(parse_throws_as<sdp::CliError>({"--dns-port", "5353"}));
    CHECK(parse_throws_as<sdp::CliError>({"--dnsv6-port", "5353"}));

    // The 30-payload cap is shared by all custom payload options.
    std::vector<std::string_view> many_payloads;
    for (int index = 0; index <= sdp::kMaxFakePackets; ++index) {
        many_payloads.push_back("--fake-gen");
        many_payloads.push_back("1");
    }
    many_payloads.push_back("--fake-ttl");
    many_payloads.push_back("5");
    CHECK(parse_throws_as<sdp::CliError>(many_payloads));
}

void test_preset_equivalence()
{
    const auto preset1 = parse({"-1"});
    const auto expansion1 = parse({"-p", "-r", "-s", "-f", "2", "-k", "2",
                                   "-n", "-e", "2"});
    CHECK(preset1.block_passive == expansion1.block_passive);
    CHECK(preset1.http.replace_host == expansion1.http.replace_host);
    CHECK(preset1.http.remove_host_space ==
          expansion1.http.remove_host_space);
    CHECK(preset1.http.fragment_size == expansion1.http.fragment_size);
    CHECK(preset1.https.fragment_size == expansion1.https.fragment_size);
    CHECK(preset1.http.persistent == expansion1.http.persistent);
    CHECK(preset1.native_frag == expansion1.native_frag);

    const auto preset2 = parse({"-2"});
    const auto expansion2 = parse({"-p", "-r", "-s", "-f", "2", "-k", "2",
                                   "-n", "-e", "40"});
    CHECK(preset2.https.fragment_size == expansion2.https.fragment_size);
    CHECK(preset2.native_frag == expansion2.native_frag);
}

void test_blacklist()
{
    const auto path = std::filesystem::temp_directory_path() /
                      "shadowdpi-blacklist-test.txt";
    {
        std::ofstream file(path);
        file << "# comment\n"
                "\n"
                "Example.COM\n"
                " .target.org \n"
                "; another comment\n"
                "only.test\n";
    }

    sdp::Blacklist blacklist;
    std::string error;
    CHECK(blacklist.load(path, error));
    CHECK(error.empty());
    CHECK(blacklist.size() == 3);
    CHECK(blacklist.matches("example.com"));
    CHECK(blacklist.matches("EXAMPLE.com"));
    CHECK(blacklist.matches("www.example.com"));
    CHECK(blacklist.matches("a.b.example.com"));
    CHECK(blacklist.matches("target.org"));
    CHECK(blacklist.matches("deep.sub.target.org"));
    CHECK(blacklist.matches("only.test"));
    CHECK(!blacklist.matches("notexample.com"));
    CHECK(!blacklist.matches("example.org"));
    CHECK(!blacklist.matches(""));

    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    sdp::Blacklist empty;
    CHECK(!empty.load(path, error));
    CHECK(!error.empty());
}

void test_auto_ttl()
{
    using sdp::TtlTracker;

    CHECK(!TtlTracker::compute_auto_ttl(128, 1, 4, 3, 10).has_value());
    CHECK(!TtlTracker::compute_auto_ttl(64, 1, 4, 3, 10).has_value());
    CHECK(!TtlTracker::compute_auto_ttl(127, 1, 4, 3, 10).has_value());
    CHECK(!TtlTracker::compute_auto_ttl(120, 1, 4, 10, 10).has_value());

    const auto normal = TtlTracker::compute_auto_ttl(120, 1, 4, 3, 10);
    CHECK(normal.has_value());
    CHECK(*normal == 4);

    const auto clamped = TtlTracker::compute_auto_ttl(100, 1, 4, 3, 10);
    CHECK(clamped.has_value());
    CHECK(*clamped == 10);

    const auto unclamped = TtlTracker::compute_auto_ttl(100, 1, 4, 3, 0);
    CHECK(unclamped.has_value());
    CHECK(*unclamped == 24);

    const auto short_hop = TtlTracker::compute_auto_ttl(60, 1, 4, 3, 10);
    CHECK(short_hop.has_value());
    CHECK(*short_hop == 2);

    const auto sixty = TtlTracker::compute_auto_ttl(60, 1, 4, 5, 10);
    CHECK(!sixty.has_value());
}

void test_ttl_tracker()
{
    sdp::TtlTracker tracker;
    const std::array<std::uint32_t, 4> remote{0x01020304, 0, 0, 0};
    const std::array<std::uint32_t, 4> local{0x0A000001, 0, 0, 0};

    tracker.record_incoming(remote, local, 443, 50000, false, 117);
    const auto ttl = tracker.lookup_outgoing(local, remote, 50000, 443, false);
    CHECK(ttl.has_value());
    CHECK(*ttl == 117);
    CHECK(!tracker.lookup_outgoing(local, remote, 50000, 443, false).has_value());

    // A retransmitted SYN/ACK refreshes the stored TTL.
    tracker.record_incoming(remote, local, 443, 50002, false, 100);
    tracker.record_incoming(remote, local, 443, 50002, false, 90);
    CHECK(tracker.size() == 1);
    const auto refreshed =
        tracker.lookup_outgoing(local, remote, 50002, 443, false);
    CHECK(refreshed.has_value());
    CHECK(*refreshed == 90);

    tracker.record_incoming(remote, local, 443, 50001, true, 55);
    CHECK(!tracker.lookup_outgoing(local, remote, 50001, 443, false).has_value());
    CHECK(tracker.lookup_outgoing(local, remote, 50001, 443, true).has_value());

    // The map is capped under a SYN/ACK flood.
    for (std::size_t index = 0; index <= sdp::TtlTracker::kMaxEntries;
         ++index) {
        tracker.record_incoming(
            remote, local, static_cast<std::uint16_t>(1000 + (index % 60000)),
            50000, false, 64);
    }
    CHECK(tracker.size() <= sdp::TtlTracker::kMaxEntries);
}

void test_dns_tracker()
{
    sdp::DnsTracker tracker;
    const std::array<std::uint32_t, 4> local{0x0100007F, 0, 0, 0};
    const std::array<std::uint32_t, 4> resolver{0x08080808, 0, 0, 0};

    const auto query = make_dns_message(false, 0x1234);
    CHECK(sdp::DnsTracker::is_dns_packet(query, true));
    CHECK(tracker.handle_outgoing(local, 53000, resolver, 53, query, false));

    const auto response = make_dns_message(true, 0x1234);
    CHECK(sdp::DnsTracker::is_dns_packet(response, false));

    // A wrong transaction ID must not consume the entry.
    const auto wrong_id = make_dns_message(true, 0x9999);
    CHECK(!tracker.handle_incoming(local, 53000, wrong_id, false).has_value());

    // A wrong question must not consume the entry either.
    auto wrong_question = response;
    wrong_question[13] = std::byte{'E'};
    wrong_question[19] = std::byte{'z'};
    CHECK(!tracker.handle_incoming(local, 53000, wrong_question, false)
               .has_value());

    const auto redirect =
        tracker.handle_incoming(local, 53000, response, false);
    CHECK(redirect.has_value());
    CHECK(redirect->remote_ip[0] == resolver[0]);
    CHECK(redirect->remote_port == 53);
    CHECK(!tracker.handle_incoming(local, 53000, response, false).has_value());

    // Non-DNS payloads must not be tracked.
    auto random = to_buffer(std::string(16, '\x42'));
    CHECK(!tracker.handle_outgoing(local, 53001, resolver, 53, random, false));

    // The map is capped under a flood.
    for (std::size_t index = 0; index <= sdp::DnsTracker::kMaxEntries;
         ++index) {
        const auto flood_query = make_dns_message(
            false, static_cast<std::uint16_t>(index));
        CHECK(tracker.handle_outgoing(
            local, static_cast<std::uint16_t>(1000 + (index % 60000)),
            resolver, 53, flood_query, false));
    }
    CHECK(tracker.size() <= sdp::DnsTracker::kMaxEntries);
}

void test_http_helpers()
{
    const auto payload = to_buffer(
        "GET / HTTP/1.1\r\nHost: example.com\r\n"
        "User-Agent: test\r\n\r\n");
    const auto span = std::span<const std::byte>{payload};

    const auto host = sdp::find_header(span, "\r\nHost: ");
    CHECK(host.has_value());
    CHECK(from_buffer(span.subspan(host->value_offset, host->value_length)) ==
          "example.com");

    const auto user_agent = sdp::find_header(span, "\r\nUser-Agent: ");
    CHECK(user_agent.has_value());
    CHECK(from_buffer(span.subspan(user_agent->value_offset,
                                   user_agent->value_length)) == "test");

    // Header names are case-insensitive.
    const auto lowercase = to_buffer(
        "GET / HTTP/1.1\r\nhost: lowercase.example\r\n\r\n");
    const auto lowercase_span =
        std::span<const std::byte>{lowercase};
    const auto lowercase_host =
        sdp::find_header(lowercase_span, "\r\nHost: ");
    CHECK(lowercase_host.has_value());
    CHECK(from_buffer(lowercase_span.subspan(lowercase_host->value_offset,
                                             lowercase_host->value_length)) ==
          "lowercase.example");

    // A header-looking sequence in the body must not win over the real header.
    const auto body_decoy = to_buffer(
        "POST / HTTP/1.1\r\nHost: real.example\r\n"
        "Content-Length: 24\r\n\r\n\r\nHost: fake.example\r\n");
    const auto body_span = std::span<const std::byte>{body_decoy};
    const auto real_host = sdp::find_header(body_span, "\r\nHost: ");
    CHECK(real_host.has_value());
    CHECK(from_buffer(body_span.subspan(real_host->value_offset,
                                        real_host->value_length)) ==
          "real.example");

    bool fragmented = true;
    const auto method_end = sdp::find_http_method_end(payload, 2, &fragmented);
    CHECK(method_end.has_value());
    CHECK(*method_end == 3);
    CHECK(!fragmented);

    auto case_buffer = to_buffer("example.com");
    sdp::mix_case(case_buffer);
    CHECK(from_buffer(case_buffer) == "eXaMpLe.cOm");

    auto host_buffer = payload;
    sdp::replace_host_name(host_buffer, *host);
    CHECK(from_buffer(host_buffer).find("\r\nhoSt: example.com") !=
          std::string::npos);

    auto space_buffer = payload;
    CHECK(sdp::move_host_space_to_user_agent(space_buffer, *host, *user_agent));
    const std::string moved = from_buffer(space_buffer);
    CHECK(moved.find("\r\nHost:example.com\r\n") != std::string::npos);
    CHECK(moved.find("User-Agent: test \r\n") != std::string::npos);

    auto extra_space_buffer = payload;
    CHECK(sdp::add_extra_space_before_uri(extra_space_buffer, *method_end,
                                          host->value_offset));
    const std::string extra = from_buffer(extra_space_buffer);
    CHECK(extra.find("GET  / HTTP/1.1\r\nHost:example.com") !=
          std::string::npos);

    const auto redirect = as_bytes(
        "HTTP/1.1 302 Found\r\nLocation: http://example.com/\r\n"
        "Connection: close\r\n\r\n");
    CHECK(sdp::is_passive_dpi_redirect(redirect));
    const auto ordinary = as_bytes("HTTP/1.1 200 OK\r\n\r\n");
    CHECK(!sdp::is_passive_dpi_redirect(ordinary));

    auto fragmented_payload = to_buffer("T / HTTP/1.1\r\n");
    bool was_fragmented = false;
    const auto fragmented_method =
        sdp::find_http_method_end(fragmented_payload, 2, &was_fragmented);
    CHECK(fragmented_method.has_value());
    CHECK(was_fragmented);
    CHECK(*fragmented_method == 1);
}

void test_tls_helpers()
{
    const auto hello = sdp::make_fake_client_hello("example.com");
    CHECK(sdp::is_tls_client_hello_prefix(hello));
    const auto sni = sdp::extract_sni(hello);
    CHECK(sni.has_value());
    CHECK(sni->hostname == "example.com");
    CHECK(sni->offset + sni->hostname.size() <= hello.size());

    // A malformed candidate before the real SNI must not abort the scan.
    std::vector<std::byte> decoy(9, std::byte{0});
    decoy[3] = std::byte{5};
    decoy[5] = std::byte{3};
    std::vector<std::byte> with_decoy = decoy;
    with_decoy.insert(with_decoy.end(), hello.begin(), hello.end());
    const auto decoy_sni = sdp::extract_sni(with_decoy);
    CHECK(decoy_sni.has_value());
    CHECK(decoy_sni->hostname == "example.com");

    const auto two_bytes = as_bytes(std::string_view{"\x16\x03", 2});
    CHECK(sdp::is_tls_client_hello_prefix(two_bytes));
    const auto garbage = as_bytes(std::string_view{"\x17\x03\x03", 3});
    CHECK(!sdp::is_tls_client_hello_prefix(garbage));

    CHECK(!sdp::default_http_fake().empty());
    CHECK(!sdp::default_https_fake().empty());
    CHECK(sdp::is_tls_client_hello_prefix(sdp::default_https_fake()));

    const auto generated = sdp::make_random_fake_payloads(3, 200);
    CHECK(generated.size() == 3);
    for (const auto& fake : generated) {
        CHECK(fake.size() >= 8);
        CHECK(fake.size() <= 199);
    }

    bool threw = false;
    try {
        (void)sdp::make_random_fake_payloads(0, 200);
    }
    catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

void test_filters()
{
    sdp::Config config;
    std::string filter = sdp::build_main_filter(config);
    CHECK(filter.find("tcp") != std::string::npos);
    CHECK(filter.find("inbound") != std::string::npos);
    CHECK(filter.find("outbound") != std::string::npos);
    CHECK(filter.find("tcp.PayloadLength > 16") != std::string::npos);
    CHECK(filter.find("tcp.PayloadLength > 0") != std::string::npos);
    CHECK(filter.find("#MINPAYLOAD#") == std::string::npos);

    config.extra_ports.push_back(8080);
    filter = sdp::build_main_filter(config);
    CHECK(filter.find("8080") != std::string::npos);
    CHECK(filter.find("tcp.Ack") != std::string::npos);
    CHECK(filter.find("tcp.Syn") != std::string::npos);

    config.max_payload = 1200;
    filter = sdp::build_main_filter(config);
    CHECK(filter.find("tcp.PayloadLength < 1200") != std::string::npos);

    config.ip_ids.push_back(5);
    filter = sdp::build_main_filter(config);
    CHECK(filter.find("ip.Id == 5") != std::string::npos);

    config.dns.v4_server = *sdp::parse_ip_address("8.8.8.8");
    filter = sdp::build_main_filter(config);
    CHECK(filter.find("udp.SrcPort == 53") != std::string::npos);

    const std::string passive = sdp::build_passive_filter(config);
    CHECK(passive.find("tcp.Rst") != std::string::npos);
    CHECK(passive.find("ip.Id == 5") != std::string::npos);
    CHECK(passive.find("(true") == std::string::npos);
    CHECK(passive.find("ip.Id >= 0x0") != std::string::npos);

    const std::string quic = sdp::build_quic_filter();
    CHECK(quic.find("udp.DstPort == 443") != std::string::npos);
    CHECK(quic.find("udp.PayloadLength >= 1200") != std::string::npos);
    CHECK(quic.find("0x6b3343cf") != std::string::npos);
}

void test_util()
{
    CHECK(sdp::replace_all("a-b-c", "-", "+") == "a+b+c");
    CHECK(sdp::replace_all("abc", "", "x") == "abc");
    CHECK(sdp::to_ascii_lower("ExAmPle.COM") == "example.com");
    CHECK(sdp::ascii_iequals("Host", "hOST"));
    CHECK(!sdp::ascii_iequals("Host", "hosts"));

    const auto ipv4 = sdp::parse_ip_address("192.168.1.1");
    CHECK(ipv4.has_value());
    CHECK(ipv4->family == sdp::IpFamily::v4);
    CHECK(sdp::to_string(*ipv4) == "192.168.1.1");

    const auto ipv6 = sdp::parse_ip_address("2001:db8::1");
    CHECK(ipv6.has_value());
    CHECK(ipv6->family == sdp::IpFamily::v6);
    CHECK(sdp::to_string(*ipv6) == "2001:db8::1");

    CHECK(!sdp::parse_ip_address("999.1.1.1").has_value());
    CHECK(!sdp::parse_ip_address("").has_value());

    const auto hex = sdp::parse_hex_payload("00ff10");
    CHECK(hex.has_value());
    CHECK(hex->size() == 3);
    CHECK(sdp::parse_hex_payload("0").has_value() == false);
    CHECK(sdp::parse_hex_payload("zz").has_value() == false);
}

} // namespace

int main()
{
    test_config_defaults();
    test_preset9_defaults();
    test_native_frag_defaults();
    test_fake_mode_validation();
    test_cli_options();
    test_preset_equivalence();
    test_blacklist();
    test_auto_ttl();
    test_ttl_tracker();
    test_dns_tracker();
    test_http_helpers();
    test_tls_helpers();
    test_filters();
    test_util();

    if (g_failures == 0) {
        std::cout << "All " << g_checks << " checks passed.\n";
        return 0;
    }

    std::cerr << g_failures << " of " << g_checks << " checks failed.\n";
    return 1;
}
