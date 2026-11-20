#include "service.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "shadowdpi/blacklist.hpp"
#include "shadowdpi/cli.hpp"
#include "shadowdpi/ipc.hpp"
#include "application.hpp"
#include "config_store.hpp"
#include "divert.hpp"
#include "ipc_pipe.hpp"
#include "lifecycle.hpp"
#include "win_headers.hpp"

namespace sdp::win {
namespace {

constexpr char kServiceName[] = "ShadowDPI";

SERVICE_STATUS g_status{};
SERVICE_STATUS_HANDLE g_status_handle{nullptr};
std::mutex g_status_mutex;
std::vector<std::string> g_arguments;
std::function<void()> g_ready_callback;
std::mutex g_ready_mutex;
std::thread g_watchdog;
std::atomic<bool> g_watchdog_stop{false};
std::atomic<bool> g_service_exiting{false};
std::vector<std::string> g_effective_arguments;
std::mutex g_journal_mutex;
std::deque<std::string> g_journal;

void write_journal(const std::string& line)
{
    std::lock_guard lock{g_journal_mutex};
    g_journal.push_back(line);
    if (g_journal.size() > 200) {
        g_journal.pop_front();
    }
}

[[nodiscard]] std::string current_preset()
{
    for (const auto& argument : g_effective_arguments) {
        if (argument.size() == 2 && argument[0] == '-' &&
            argument[1] >= '1' && argument[1] <= '9') {
            return argument;
        }
    }
    return {};
}

void report_status() noexcept
{
    if (g_status_handle != nullptr) {
        SetServiceStatus(g_status_handle, &g_status);
    }
}

/// Optional privileged-command delegation. Absent means admins only; status
/// reads remain available to authenticated users either way.
[[nodiscard]] std::optional<std::string> read_allowed_sid() noexcept
{
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\ShadowDPI", 0,
                      KEY_READ, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    char buffer[256]{};
    DWORD length = sizeof(buffer);
    DWORD type = 0;
    const LONG result = RegQueryValueExA(key, "IpcAllowedSid", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(buffer),
                                         &length);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || length == 0 || length >= sizeof(buffer) ||
        (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return std::nullopt;
    }
    if (buffer[length - 1] == '\0') {
        --length;
    }
    return std::string(buffer, length);
}

[[nodiscard]] sdp::ipc::Response handle_ipc_request(
    const sdp::ipc::Request& request,
    const sdp::ipc::Caller& caller) noexcept
{
    using sdp::ipc::MessageType;
    using sdp::ipc::Response;

    Response response;
    if (!caller.authenticated) {
        response.message = "caller identity unavailable";
        return response;
    }
    if (!sdp::ipc::caller_may_run(caller, request.type)) {
        response.message = "not permitted";
        return response;
    }

    switch (request.type) {
    case MessageType::Handshake:
        response.ok = true;
        response.message = "version=" +
                           std::to_string(sdp::ipc::kIpcVersion) +
                           ";pid=" + std::to_string(request.pid);
        break;

    case MessageType::GetStatus:
        {
            nlohmann::json status;
            {
                std::lock_guard lock{g_status_mutex};
                status["state"] = g_status.dwCurrentState;
                status["exit"] = g_status.dwWin32ExitCode;
                status["checkpoint"] = g_status.dwCheckPoint;
            }
            status["pid"] = GetCurrentProcessId();
            status["preset"] = current_preset();
            response.ok = true;
            response.message = status.dump();
        }
        break;

    case MessageType::GetLog:
        {
            nlohmann::json log;
            log["lines"] = nlohmann::json::array();
            {
                std::lock_guard lock{g_journal_mutex};
                for (const auto& line : g_journal) {
                    log["lines"].push_back(line);
                }
            }
            response.ok = true;
            response.message = log.dump();
        }
        break;

    case MessageType::ApplyConfig:
        {
            const auto received = sdp::ipc::decode_argv(request.payload);
            if (!received) {
                response.message = "malformed arguments";
                break;
            }
            std::vector<std::string> arguments{"shadowdpi.exe"};
            arguments.insert(arguments.end(), received->begin(),
                             received->end());
            std::vector<char*> argv;
            argv.reserve(arguments.size());
            for (auto& argument : arguments) {
                argv.push_back(argument.data());
            }
            try {
                (void)sdp::parse_cli(static_cast<int>(argv.size()),
                                     argv.data());
            }
            catch (const sdp::CliError& error) {
                response.message = error.what();
                break;
            }
            std::string error;
            if (!sdp::ipc::save_config(sdp::ipc::default_config_path(),
                                       arguments, error)) {
                response.message = error;
                break;
            }
            write_journal("configuration saved via IPC");
            response.ok = true;
            response.message = "saved; restart required";
        }
        break;

    case MessageType::Stop:
        write_journal("stop requested via IPC");
        ShutdownCoordinator::instance().request_stop();
        response.ok = true;
        response.message = "stop requested";
        break;

    case MessageType::WriteBlacklist:
        {
            std::string text;
            text.reserve(request.payload.size());
            for (const auto byte : request.payload) {
                text.push_back(static_cast<char>(
                    std::to_integer<unsigned char>(byte)));
            }
            const auto target = sdp::ipc::default_blacklist_path();
            const auto temporary = target.string() + ".tmp";
            std::size_t entry_count = 0;
            const std::string error = [&]() -> std::string {
                try {
                    std::filesystem::create_directories(target.parent_path());
                    {
                        std::ofstream file(temporary,
                                           std::ios::binary |
                                               std::ios::trunc);
                        if (!file) {
                            return "cannot create blacklist file";
                        }
                        file.write(text.data(),
                                   static_cast<std::streamsize>(text.size()));
                        file.flush();
                        if (!file) {
                            return "cannot write blacklist file";
                        }
                    }
                    sdp::Blacklist blacklist;
                    std::string load_error;
                    if (!blacklist.load(temporary, load_error)) {
                        return load_error;
                    }
                    entry_count = blacklist.size();
                    std::filesystem::rename(temporary, target);
                    return {};
                }
                catch (const std::exception& exception) {
                    return exception.what();
                }
            }();
            if (!error.empty()) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                response.message = error;
            }
            else {
                write_journal("blacklist saved via IPC, " +
                              std::to_string(entry_count) + " entries");
                response.ok = true;
                response.message = "blacklist saved; restart required";
            }
        }
        break;

    case MessageType::Start:
    case MessageType::RunScan:
        response.message = "not available in this phase";
        break;

    default:
        response.message = "unknown command";
        break;
    }
    return response;
}

void run_stop_watchdog() noexcept
{
    while (!g_watchdog_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (g_watchdog_stop.load(std::memory_order_acquire)) {
            break;
        }
        std::lock_guard lock{g_status_mutex};
        if (g_status.dwCurrentState != SERVICE_STOP_PENDING) {
            break;
        }
        ++g_status.dwCheckPoint;
        report_status();
    }
}

void WINAPI service_control(DWORD control)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        {
            std::lock_guard lock{g_status_mutex};
            g_status.dwCurrentState = SERVICE_STOP_PENDING;
            g_status.dwWaitHint = 5000;
            ++g_status.dwCheckPoint;
            report_status();
            if (!g_service_exiting.load(std::memory_order_acquire)) {
                g_watchdog_stop.store(false, std::memory_order_release);
                if (!g_watchdog.joinable()) {
                    g_watchdog = std::thread{run_stop_watchdog};
                }
            }
        }
        ShutdownCoordinator::instance().request_stop();
        break;

    case SERVICE_CONTROL_INTERROGATE:
        {
            std::lock_guard lock{g_status_mutex};
            report_status();
        }
        break;

    default:
        break;
    }
}

void WINAPI service_main(DWORD /*argc*/, LPSTR* /*argv*/)
{
    {
        std::lock_guard lock{g_status_mutex};
        g_status = {};
        g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        g_status.dwCurrentState = SERVICE_START_PENDING;
        g_status.dwCheckPoint = 1;
        g_status.dwWaitHint = 10000;

        g_status_handle =
            RegisterServiceCtrlHandlerA(kServiceName, service_control);
        if (g_status_handle == nullptr) {
            g_status.dwCurrentState = SERVICE_STOPPED;
            g_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            g_status.dwServiceSpecificExitCode = 1;
            return;
        }
        report_status();
    }

    set_service_ready_callback([] {
        std::lock_guard lock{g_status_mutex};
        if (g_status.dwCurrentState != SERVICE_START_PENDING) {
            return;
        }
        g_status.dwCurrentState = SERVICE_RUNNING;
        g_status.dwControlsAccepted =
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
        g_status.dwCheckPoint = 0;
        g_status.dwWaitHint = 0;
        report_status();
        write_journal("service running");
    });

    write_journal("service starting");
    const auto config_writer = read_allowed_sid();
    std::unique_ptr<sdp::ipc::IpcServer> ipc;
    std::string ipc_error;
    ipc = std::make_unique<sdp::ipc::IpcServer>(
        "shadowdpi", sdp::ipc::build_pipe_sddl(config_writer), config_writer);
    ipc->set_handler(handle_ipc_request);
    if (!ipc->start(ipc_error)) {
        write_journal("IPC unavailable: " + ipc_error);
        OutputDebugStringA(("ShadowDPI IPC unavailable: " + ipc_error).c_str());
    }

    std::vector<std::string> effective_arguments = g_arguments;
    std::string config_error;
    const auto stored =
        sdp::ipc::load_config(sdp::ipc::default_config_path(), config_error);
    if (stored && !stored->empty()) {
        effective_arguments = *stored;
        write_journal("configuration loaded from config.json");
    }
    else {
        write_journal("configuration taken from service command line");
    }
    const auto blacklist = sdp::ipc::default_blacklist_path();
    if (std::filesystem::exists(blacklist)) {
        effective_arguments.push_back("--blacklist");
        effective_arguments.push_back(blacklist.string());
        write_journal("using saved blacklist");
    }
    g_effective_arguments = effective_arguments;

    std::vector<char*> argv;
    argv.reserve(effective_arguments.size() + 1);
    for (auto& argument : effective_arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const int result =
        run_application(static_cast<int>(effective_arguments.size()),
                        argv.data());
    write_journal("service stopped, exit " + std::to_string(result));

    if (ipc) {
        ipc->stop();
    }

    {
        std::lock_guard lock{g_status_mutex};
        g_service_exiting.store(true, std::memory_order_release);
        g_watchdog_stop.store(true, std::memory_order_release);
    }
    if (g_watchdog.joinable()) {
        g_watchdog.join();
    }

    {
        std::lock_guard lock{g_status_mutex};
        g_status.dwWin32ExitCode =
            result == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR;
        g_status.dwServiceSpecificExitCode = static_cast<DWORD>(result);
        g_status.dwCurrentState = SERVICE_STOPPED;
        g_status.dwControlsAccepted = 0;
        g_status.dwCheckPoint = 0;
        g_status.dwWaitHint = 0;
        report_status();
    }
    set_service_ready_callback({});
}

} // namespace

void set_service_ready_callback(std::function<void()> callback)
{
    std::lock_guard lock{g_ready_mutex};
    g_ready_callback = std::move(callback);
}

void signal_service_ready() noexcept
{
    std::function<void()> callback;
    {
        std::lock_guard lock{g_ready_mutex};
        callback = g_ready_callback;
    }
    if (callback) {
        try {
            callback();
        }
        catch (...) {
        }
    }
}

std::optional<int> run_as_service_if_requested(int argc, char* argv[])
{
    g_arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        g_arguments.emplace_back(argv[index]);
    }

    SERVICE_TABLE_ENTRYA table[] = {
        {const_cast<char*>(kServiceName), service_main},
        {nullptr, nullptr},
    };

    if (StartServiceCtrlDispatcherA(table)) {
        return 0;
    }

    const DWORD error = GetLastError();
    if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        // Normal console launch.
        return std::nullopt;
    }

    std::cerr << "error: cannot connect to the service control manager: "
              << format_error_message(error) << '\n';
    return 1;
}

} // namespace sdp::win
