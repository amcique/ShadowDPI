#include "application.hpp"
#include "lifecycle.hpp"
#include "service.hpp"
#include "win_headers.hpp"

namespace {

// SetDefaultDllDirectories is missing on Windows 7 without KB2533623, so it is
// resolved at run time. On those systems SetDllDirectoryA("") still applies.
void restrict_dll_search_path() noexcept
{
    using SetDefaultDllDirectoriesFn = BOOL(WINAPI*)(DWORD);

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 != nullptr) {
        const auto set_default = reinterpret_cast<SetDefaultDllDirectoriesFn>(
            GetProcAddress(kernel32, "SetDefaultDllDirectories"));
        if (set_default != nullptr) {
            set_default(LOAD_LIBRARY_SEARCH_SYSTEM32 |
                        LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
        }
    }

    SetDllDirectoryA("");
    SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE |
                      BASE_SEARCH_PATH_PERMANENT);
}

} // namespace

int main(int argc, char* argv[])
{
    // Only load DLLs from safe locations, never from the current directory.
    restrict_dll_search_path();

    // Reset before the service dispatcher starts, so an early IPC stop request
    // is not cleared after the service has begun starting.
    sdp::win::ShutdownCoordinator::instance().reset();

    if (const auto code = sdp::win::run_as_service_if_requested(argc, argv)) {
        return *code;
    }

    return sdp::win::run_application(argc, argv);
}
