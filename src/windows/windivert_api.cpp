#include "windivert_api.hpp"

namespace sdp::win {
namespace {

WinDivertApi g_api;
HMODULE g_module{nullptr};

/// Local copy of the system-message formatting used by divert.cpp, so this
/// module stays linkable from the packet tools without pulling in WinDivert
/// handle code.
[[nodiscard]] std::string describe_last_error()
{
    const DWORD error = GetLastError();
    LPSTR buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

    std::string message = "Win32 error " + std::to_string(error);
    if (length != 0 && buffer != nullptr) {
        std::string detail{buffer, length};
        while (!detail.empty() &&
               (detail.back() == '\r' || detail.back() == '\n' ||
                detail.back() == ' ')) {
            detail.pop_back();
        }
        if (!detail.empty()) {
            message += ": " + detail;
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return message;
}

template <typename T>
[[nodiscard]] bool resolve(HMODULE module, const char* name, T& target) noexcept
{
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    return target != nullptr;
}

} // namespace

bool load_windivert_api(std::string& error) noexcept
{
    if (g_module != nullptr) {
        return true;
    }

    constexpr DWORD kSearchFlags =
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;

    HMODULE module = LoadLibraryExA("WinDivert.dll", nullptr, kSearchFlags);
    if (module == nullptr && GetLastError() == ERROR_INVALID_PARAMETER) {
        // Pre-Windows 8 systems without the LOAD_LIBRARY_SEARCH flags still
        // get the SetDllDirectoryA("") protection applied in main().
        module = LoadLibraryA("WinDivert.dll");
    }
    if (module == nullptr) {
        error = "cannot load WinDivert.dll from the application or system "
                "directory: " + describe_last_error();
        return false;
    }

    WinDivertApi api;
    const bool complete =
        resolve(module, "WinDivertOpen", api.open) &&
        resolve(module, "WinDivertClose", api.close) &&
        resolve(module, "WinDivertRecv", api.recv) &&
        resolve(module, "WinDivertSend", api.send) &&
        resolve(module, "WinDivertShutdown", api.shutdown) &&
        resolve(module, "WinDivertHelperParsePacket", api.helper_parse) &&
        resolve(module, "WinDivertHelperCalcChecksums",
                api.helper_calc_checksums);

    if (!complete) {
        error = "WinDivert.dll is missing required entry points: " +
                describe_last_error();
        FreeLibrary(module);
        return false;
    }

    g_api = api;
    g_module = module;
    return true;
}

const WinDivertApi& windivert_api() noexcept
{
    return g_api;
}

} // namespace sdp::win
