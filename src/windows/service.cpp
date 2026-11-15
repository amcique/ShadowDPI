#include "service.hpp"

#include <iostream>
#include <mutex>
#include <string>
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

void WINAPI service_control(DWORD control)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        {
            std::lock_guard lock{g_status_mutex};
            g_status.dwCurrentState = SERVICE_STOP_PENDING;
            g_status.dwWaitHint = 5000;
            g_status.dwCheckPoint += 1;
            if (g_status_handle != nullptr) {
                SetServiceStatus(g_status_handle, &g_status);
            }
        }
        ShutdownCoordinator::instance().request_stop();
        break;

    case SERVICE_CONTROL_INTERROGATE:
        {
            std::lock_guard lock{g_status_mutex};
            if (g_status_handle != nullptr) {
                SetServiceStatus(g_status_handle, &g_status);
            }
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
        g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        g_status.dwCurrentState = SERVICE_RUNNING;
        g_status.dwControlsAccepted =
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
        g_status.dwWin32ExitCode = 0;
        g_status.dwServiceSpecificExitCode = 0;
        g_status.dwCheckPoint = 1;
        g_status.dwWaitHint = 0;

        g_status_handle =
            RegisterServiceCtrlHandlerA(kServiceName, service_control);
        if (g_status_handle == nullptr) {
            return;
        }
        SetServiceStatus(g_status_handle, &g_status);
    }

    std::vector<char*> argv;
    argv.reserve(g_arguments.size() + 1);
    for (auto& argument : g_arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const int result = run_application(static_cast<int>(g_arguments.size()),
                                       argv.data());

    std::lock_guard lock{g_status_mutex};
    g_status.dwWin32ExitCode =
        result == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR;
    g_status.dwServiceSpecificExitCode = static_cast<DWORD>(result);
    g_status.dwCurrentState = SERVICE_STOPPED;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    SetServiceStatus(g_status_handle, &g_status);
}

} // namespace

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
