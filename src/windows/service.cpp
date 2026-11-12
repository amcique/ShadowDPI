#include "service.hpp"

#include <string>
#include <vector>

#include "application.hpp"
#include "lifecycle.hpp"
#include "win_headers.hpp"

namespace sdp::win {
namespace {

constexpr char kServiceName[] = "ShadowDPI";

SERVICE_STATUS g_status{};
SERVICE_STATUS_HANDLE g_status_handle{nullptr};
std::vector<std::string> g_arguments;

void WINAPI service_control(DWORD control)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        g_status.dwCurrentState = SERVICE_STOP_PENDING;
        g_status.dwWaitHint = 5000;
        g_status.dwCheckPoint += 1;
        SetServiceStatus(g_status_handle, &g_status);
        ShutdownCoordinator::instance().request_stop();
        break;
    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(g_status_handle, &g_status);
        break;
    default:
        break;
    }
}

void WINAPI service_main(DWORD /*argc*/, LPSTR* /*argv*/)
{
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

    std::vector<char*> argv;
    argv.reserve(g_arguments.size() + 1);
    for (auto& argument : g_arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const int result = run_application(static_cast<int>(g_arguments.size()),
                                       argv.data());

    g_status.dwWin32ExitCode =
        result == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR;
    g_status.dwServiceSpecificExitCode = static_cast<DWORD>(result);
    g_status.dwCurrentState = SERVICE_STOPPED;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;
    SetServiceStatus(g_status_handle, &g_status);
}

} // namespace

bool run_as_service_if_requested(int argc, char* argv[])
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
        return true;
    }

    // ERROR_FAILED_SERVICE_CONTROLLER_CONNECT means we were started from a
    // console, which is the normal path.
    return false;
}

} // namespace sdp::win
