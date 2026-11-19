#include "service.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "application.hpp"
#include "divert.hpp"
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

    std::vector<char*> argv;
    argv.reserve(g_arguments.size() + 1);
    for (auto& argument : g_arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const int result = run_application(static_cast<int>(g_arguments.size()),
                                       argv.data());

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
