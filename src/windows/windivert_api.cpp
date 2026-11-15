#include "windivert_api.hpp"

#include <array>
#include <vector>

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

/// Full path of a loaded module, or an empty string when the path cannot be
/// read. The buffer grows instead of failing on paths over MAX_PATH.
[[nodiscard]] std::string loaded_module_path(HMODULE module)
{
    std::vector<char> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameA(
            module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            return std::string(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
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

bool check_windivert_runtime(std::string& error) noexcept
{
    HMODULE module = GetModuleHandleA("WinDivert.dll");
    if (module == nullptr) {
        error = "WinDivert.dll is not loaded";
        return false;
    }

    const std::string dll_path = loaded_module_path(module);
    const std::size_t separator = dll_path.find_last_of("\\/");
    if (dll_path.empty() || separator == std::string::npos) {
        error = "cannot locate the loaded WinDivert.dll; reinstall ShadowDPI "
                "from the release archive";
        return false;
    }

    const std::string directory = dll_path.substr(0, separator + 1);
    constexpr std::array<const char*, 2> kDriverNames{"WinDivert64.sys",
                                                      "WinDivert.sys"};
    for (const char* name : kDriverNames) {
        const std::string candidate = directory + name;
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return true;
        }
    }

    error = "WinDivert64.sys was not found next to WinDivert.dll (" +
            directory +
            "). Unpack the release archive again, or copy both files "
            "together.";
    return false;
}

} // namespace sdp::win
