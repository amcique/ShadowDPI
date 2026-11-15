#include "application.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "shadowdpi/blacklist.hpp"
#include "shadowdpi/cli.hpp"
#include "shadowdpi/config.hpp"
#include "shadowdpi/filter.hpp"
#include "shadowdpi/log.hpp"
#include "shadowdpi/version.hpp"

#include "divert.hpp"
#include "lifecycle.hpp"
#include "packet.hpp"
#include "packet_engine.hpp"
#include "win_headers.hpp"
#include "windivert_api.hpp"

namespace sdp::win {
namespace {

template <typename T>
void print_setting(std::string_view name, const T& value)
{
    std::cout << name << ": " << value << '\n';
}

[[nodiscard]] const char* yes_no(bool value) noexcept
{
    return value ? "yes" : "no";
}

/// Thin adapter between the packet engine and the WinDivert main handle.
class DivertSink final : public PacketSink {
public:
    explicit DivertSink(DivertHandle& handle) noexcept
        : handle_(handle)
    {
    }

    bool send(const PacketBuffer& packet) override
    {
        return handle_.send(packet.data.data(), packet.length, packet.address);
    }

private:
    DivertHandle& handle_;
};

/// Serializes callback lifetime with Application destruction. The stop callback
/// may run on a console or service control thread, so destruction must wait for
/// an in-flight callback before the object goes away.
struct StopGate {
    std::mutex mutex;
    bool active{true};
};

} // namespace

class Application {
public:
    explicit Application(Config config)
        : config_(std::move(config))
        , logger_(config_.verbose)
        , sink_(main_handle_)
        , engine_(config_, logger_, sink_)
    {
    }

    ~Application();

    int run();

private:
    void print_banner() const;
    void print_summary() const;
    void load_blacklists();
    void flush_dns_cache();
    bool open_filters();
    void write_ready_file() const;
    void shutdown_filters() noexcept;
    [[nodiscard]] int loop();

    void process(PacketBuffer& packet);
    void reinject(PacketBuffer& packet, const PacketAction& action);
    void note_send_failure();
    bool open_drop_filter(std::string_view filter,
                          std::string_view description);

    Config config_;
    Logger logger_;
    DivertHandle main_handle_;
    std::vector<DivertHandle> drop_handles_;
    std::mutex handles_mutex_;
    std::shared_ptr<StopGate> stop_gate_{std::make_shared<StopGate>()};
    DivertSink sink_;
    PacketEngine engine_;
    std::uint64_t send_failures_{0};
};

Application::~Application()
{
    {
        std::lock_guard lock{stop_gate_->mutex};
        stop_gate_->active = false;
    }
    ShutdownCoordinator::instance().set_stop_callback({});
}

int Application::run()
{
    print_banner();

    if (config_.help) {
        std::cout << help_text();
        return 0;
    }
    if (config_.version) {
        std::cout << version_text();
        return 0;
    }

    load_blacklists();
    print_summary();

    if (config_.dns.v4_server || config_.dns.v6_server) {
        flush_dns_cache();
    }

    if (config_.http.fragment_enabled && config_.http.fragment_size > 2 &&
        !config_.native_frag) {
        logger_.warning(
            "HTTP fragmentation values above 2 are not fully compatible "
            "with the other options; use 2 or less, or enable native "
            "fragmentation.");
    }

    install_console_stop_handler();

    if (!open_filters()) {
        return 1;
    }

    // Register only after the handles exist, so a stop request cannot race
    // with their creation. The gate keeps the callback from touching a
    // destroyed Application.
    const auto gate = stop_gate_;
    ShutdownCoordinator::instance().set_stop_callback([this, gate] {
        std::lock_guard lock{gate->mutex};
        if (gate->active) {
            shutdown_filters();
        }
    });

    write_ready_file();

    if (config_.debug_exit) {
        logger_.info("Debug exit: filters opened successfully.");
        return 0;
    }

    logger_.info("Filter activated, ShadowDPI is now running!");
    const int result = loop();

    shutdown_filters();
    logger_.info(result == 0 ? "ShadowDPI stopped."
                             : "ShadowDPI stopped after an error.");
    return result;
}

void Application::print_banner() const
{
    std::cout << kProgramName << ' ' << kVersion
              << ": Passive DPI blocker and Active DPI circumvention utility\n"
                 "https://github.com/amcique/ShadowDPI\n\n";
}

void Application::print_summary() const
{
    const auto& fake = config_.fake;
    const char* fake_mode = fake.auto_ttl.enabled
                                ? "auto"
                                : (fake.enabled ? "fixed" : "disabled");

    print_setting("Block passive DPI", yes_no(config_.block_passive));
    print_setting("Block QUIC/HTTP3", yes_no(config_.block_quic));
    print_setting("Fragment HTTP", config_.http.fragment_enabled
                                       ? config_.http.fragment_size
                                       : 0);
    print_setting("Fragment persistent HTTP",
                  config_.http.persistent ? config_.http.fragment_size : 0);
    print_setting("Fragment HTTPS", config_.https.fragment_enabled
                                        ? config_.https.fragment_size
                                        : 0);
    print_setting("Fragment by SNI", yes_no(config_.https.by_sni));
    print_setting("Native fragmentation (splitting)",
                  yes_no(config_.native_frag));
    print_setting("Fragments sending in reverse",
                  yes_no(config_.reverse_frag));
    print_setting("Replace Host with hoSt",
                  yes_no(config_.http.replace_host));
    print_setting("Host no space", yes_no(config_.http.remove_host_space));
    print_setting("Additional space", yes_no(config_.http.extra_space));
    print_setting("Mix Host case", yes_no(config_.http.mixed_case_host));
    print_setting("HTTP all ports", yes_no(config_.http.all_ports));
    print_setting("DNS redirect", yes_no(config_.dns.v4_server.has_value()));
    print_setting("DNSv6 redirect",
                  yes_no(config_.dns.v6_server.has_value()));
    print_setting("Allow missing SNI", yes_no(config_.allow_no_sni));

    std::cout << "Fake requests, TTL: " << fake_mode
              << " (fixed: " << (fake.fixed_ttl ? *fake.fixed_ttl : 0)
              << ", auto: " << static_cast<int>(fake.auto_ttl.first) << '-'
              << static_cast<int>(fake.auto_ttl.second) << '-'
              << static_cast<int>(fake.auto_ttl.max)
              << ", min distance: " << static_cast<int>(fake.min_ttl)
              << ")\n";

    print_setting("Fake requests, wrong checksum",
                  yes_no(fake.wrong_checksum));
    print_setting("Fake requests, wrong SEQ/ACK", yes_no(fake.wrong_seq));
    print_setting("Fake requests, custom payloads", fake.payloads.size());
    print_setting("Fake requests, resend",
                  static_cast<unsigned int>(fake.resend));
    print_setting("Max payload size", config_.max_payload);
    std::cout << '\n';

    if (fake.resend == 1) {
        logger_.info("note: fake-resend is 1, no resending takes place.");
    }
    else if (fake.resend == 0 && fake.enabled) {
        logger_.warning(
            "fake-resend is 0: fake packet mode has no effect.");
    }
    else if (fake.resend > 100) {
        logger_.warning("fake-resend value is very high.");
    }
}

void Application::load_blacklists()
{
    Blacklist blacklist;
    bool enabled = false;
    for (const auto& path : config_.blacklists) {
        std::string error;
        if (!blacklist.load(path, error)) {
            throw std::runtime_error(error);
        }
        enabled = true;
    }

    if (enabled) {
        logger_.info("Loaded " + std::to_string(blacklist.size()) +
                     " blacklist entries.");
    }
    engine_.set_blacklist(std::move(blacklist), enabled);
}

void Application::flush_dns_cache()
{
    HMODULE dnsapi =
        LoadLibraryExA("dnsapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dnsapi == nullptr && GetLastError() == ERROR_INVALID_PARAMETER) {
        dnsapi = LoadLibraryA("dnsapi.dll");
    }
    if (dnsapi == nullptr) {
        logger_.warning("cannot load dnsapi.dll to flush the DNS cache");
        return;
    }

    using FlushResolverCache = BOOL(WINAPI*)();
    const auto flush = reinterpret_cast<FlushResolverCache>(
        GetProcAddress(dnsapi, "DnsFlushResolverCache"));
    if (flush == nullptr || !flush()) {
        logger_.warning("cannot flush the DNS resolver cache");
    }
    FreeLibrary(dnsapi);
}

bool Application::open_drop_filter(std::string_view filter,
                                   std::string_view description)
{
    std::string error;
    auto handle = DivertHandle::open(filter, WINDIVERT_LAYER_NETWORK, 0,
                                     WINDIVERT_FLAG_DROP, error);
    if (!handle) {
        logger_.error("cannot open " + std::string(description) +
                      " filter: " + error);
        return false;
    }

    std::lock_guard lock{handles_mutex_};
    drop_handles_.push_back(std::move(*handle));
    return true;
}

bool Application::open_filters()
{
    logger_.info("Opening filters...");

    std::string load_error;
    if (!load_windivert_api(load_error)) {
        logger_.error(load_error);
        return false;
    }

    std::string runtime_error;
    if (!check_windivert_runtime(runtime_error)) {
        logger_.error(runtime_error);
        return false;
    }

    if (config_.block_passive &&
        !open_drop_filter(build_passive_filter(config_), "passive DPI")) {
        return false;
    }

    if (config_.block_quic &&
        !open_drop_filter(build_quic_filter(), "QUIC")) {
        return false;
    }

    std::string error;
    auto main_handle = DivertHandle::open(build_main_filter(config_),
                                          WINDIVERT_LAYER_NETWORK, 0, 0,
                                          error);
    if (!main_handle) {
        logger_.error("cannot open main filter: " + error);
        return false;
    }

    std::lock_guard lock{handles_mutex_};
    main_handle_ = std::move(*main_handle);
    return true;
}

void Application::shutdown_filters() noexcept
{
    // Only shut the handles down; destroying them here would race with the
    // packet loop if the request comes from a system callback thread.
    std::lock_guard lock{handles_mutex_};
    for (auto& handle : drop_handles_) {
        handle.shutdown();
    }
    main_handle_.shutdown();
}

void Application::write_ready_file() const
{
    if (config_.ready_file.empty()) {
        return;
    }

    std::ofstream file(config_.ready_file, std::ios::trunc);
    if (!file) {
        logger_.warning("cannot write ready file: " +
                        config_.ready_file.string());
        return;
    }

    file << kProgramName << ' ' << kVersion << '\n';
    file << "pid " << GetCurrentProcessId() << '\n';
    file.flush();
    if (!file) {
        logger_.warning("cannot write ready file: " +
                        config_.ready_file.string());
    }
}

int Application::loop()
{
    PacketBuffer packet;
    while (!ShutdownCoordinator::instance().stop_requested()) {
        if (!main_handle_.receive(packet)) {
            if (ShutdownCoordinator::instance().stop_requested()) {
                return 0;
            }
            logger_.error("error receiving packet: " + last_error_message());
            return 1;
        }

        process(packet);
    }
    return 0;
}

void Application::process(PacketBuffer& packet)
{
    const PacketAction action = engine_.process(packet);
    if (action.reinject) {
        reinject(packet, action);
    }
}

void Application::reinject(PacketBuffer& packet,
                           const PacketAction& action)
{
    if (action.recalculate_checksums) {
        recalculate_checksums(packet.data.data(), packet.length,
                              packet.address);
    }
    if (!main_handle_.send(packet.data.data(), packet.length,
                           packet.address)) {
        note_send_failure();
    }
}

void Application::note_send_failure()
{
    ++send_failures_;
    if (send_failures_ == 1 || send_failures_ % 1000 == 0) {
        logger_.warning("cannot reinject packet: " + last_error_message() +
                        " (" + std::to_string(send_failures_) +
                        " send failures)");
    }
}

int run_application(int argc, char* argv[])
{
    try {
        Config config = parse_cli(argc, argv);
        Application application{std::move(config)};
        return application.run();
    }
    catch (const CliError& error) {
        std::cerr << "error: " << error.what() << "\n";
        std::cerr << "Try 'shadowdpi.exe --help' for usage.\n";
        return 1;
    }
    catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

} // namespace sdp::win
