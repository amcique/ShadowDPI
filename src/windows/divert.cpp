#include "divert.hpp"

#include <utility>

#include "windivert_api.hpp"

namespace sdp::win {

DivertHandle::~DivertHandle()
{
    close();
}

DivertHandle::DivertHandle(DivertHandle&& other) noexcept
    : handle_(other.handle_)
{
    other.handle_ = INVALID_HANDLE_VALUE;
}

DivertHandle& DivertHandle::operator=(DivertHandle&& other) noexcept
{
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
    }
    return *this;
}

std::optional<DivertHandle> DivertHandle::open(std::string_view filter,
                                               WINDIVERT_LAYER layer,
                                               std::int16_t priority,
                                               std::uint64_t flags,
                                               std::string& error)
{
    const auto& api = windivert_api();
    if (api.open == nullptr) {
        error = "WinDivert is not loaded";
        return std::nullopt;
    }

    const std::string filter_string{filter};
    HANDLE handle = api.open(filter_string.c_str(), layer, priority, flags);
    const DWORD error_code = GetLastError();
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        error = format_error_message(error_code);
        const std::string hint = divert_error_hint(error_code);
        if (!hint.empty()) {
            error += "\n  hint: " + hint;
        }
        return std::nullopt;
    }

    DivertHandle result;
    result.handle_ = handle;
    return result;
}

bool DivertHandle::valid() const noexcept
{
    return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
}

bool DivertHandle::receive(PacketBuffer& packet) const noexcept
{
    packet.length = 0;
    const auto& api = windivert_api();
    if (api.recv == nullptr) {
        return false;
    }
    return api.recv(handle_, packet.data.data(),
                    static_cast<UINT>(packet.data.size()),
                    &packet.length, &packet.address) != FALSE;
}

bool DivertHandle::send(const std::byte* data,
                        UINT length,
                        const WINDIVERT_ADDRESS& address) const noexcept
{
    const auto& api = windivert_api();
    if (api.send == nullptr) {
        return false;
    }
    return api.send(handle_, data, length, nullptr, &address) != FALSE;
}

void DivertHandle::shutdown() noexcept
{
    const auto& api = windivert_api();
    if (valid() && api.shutdown != nullptr) {
        api.shutdown(handle_, WINDIVERT_SHUTDOWN_BOTH);
    }
}

void DivertHandle::close() noexcept
{
    const auto& api = windivert_api();
    if (valid() && api.close != nullptr) {
        api.close(handle_);
    }
    handle_ = INVALID_HANDLE_VALUE;
}

std::string last_error_message()
{
    return format_error_message(GetLastError());
}

std::string format_error_message(DWORD error)
{
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

std::string divert_error_hint(DWORD error)
{
    switch (error) {
    case 2:
        return "The WinDivert64.sys driver was not found next to the "
               "executable.";
    case 654:
        return "An incompatible version of the WinDivert driver is already "
               "loaded. Run as administrator: sc stop windivert && sc delete "
               "windivert (and windivert14).";
    case 577:
        return "Could not load the driver due to an invalid digital "
               "signature.";
    case 1275:
        return "The WinDivert driver is blocked by security software or the "
               "environment does not support kernel drivers.";
    case 1753:
        return "The Base Filtering Engine service is disabled. Enable it and "
               "try again.";
    default:
        return {};
    }
}

} // namespace sdp::win
