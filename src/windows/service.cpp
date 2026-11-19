#include "service.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

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

void report_status() noexcept
{
    if (g_status_handle != nullptr) {
        SetServiceStatus(g_status_handle, &g_status);
    }
}

/// Optional delegation added by the installer. Absent means admins only.
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

    const bool privileged =
        request.type == MessageType::ApplyConfig ||
        request.type == MessageType::Start ||
        request.type == MessageType::Stop ||
        request.type == MessageType::RunScan ||
        request.type == MessageType::GetLog;
    if (privileged && !caller.admin && !caller.config_writer) {
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
            std::lock_guard lock{g_status_mutex};
            response.ok = true;
            response.message =
                "state=" + std::to_string(g_status.dwCurrentState) +
                ";exit=" + std::to_string(g_status.dwWin32ExitCode) +
                ";checkpoint=" + std::to_string(g_status.dwCheckPoint) +
                ";pid=" + std::to_string(GetCurrentProcessId());
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
            response.ok = true;
            response.message = "saved; restart required";
        }
        break;

    case MessageType::Stop:
        ShutdownCoordinator::instance().request_stop();
        response.ok = true;
        response.message = "stop requested";
        break;

    case MessageType::Start:
    case MessageType::RunScan:
    case MessageType::GetLog:
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
    });

    const auto config_writer = read_allowed_sid();
    std::unique_ptr<sdp::ipc::IpcServer> ipc;
    std::string ipc_error;
    ipc = std::make_unique<sdp::ipc::IpcServer>(
        "shadowdpi", sdp::ipc::build_pipe_sddl(config_writer), config_writer);
    ipc->set_handler(handle_ipc_request);
    if (!ipc->start(ipc_error)) {
        OutputDebugStringA(("ShadowDPI IPC unavailable: " + ipc_error).c_str());
    }

    std::vector<std::string> effective_arguments = g_arguments;
    std::string config_error;
    const auto stored =
        sdp::ipc::load_config(sdp::ipc::default_config_path(), config_error);
    if (stored && !stored->empty()) {
        effective_arguments = *stored;
    }

    std::vector<char*> argv;
    argv.reserve(effective_arguments.size() + 1);
    for (auto& argument : effective_arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const int result =
        run_application(static_cast<int>(effective_arguments.size()),
                        argv.data());

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
