#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "packet.hpp"

namespace sdp::win {

/// RAII wrapper around a WinDivert handle.
class DivertHandle {
public:
    DivertHandle() noexcept = default;
    ~DivertHandle();

    DivertHandle(const DivertHandle&) = delete;
    DivertHandle& operator=(const DivertHandle&) = delete;

    DivertHandle(DivertHandle&& other) noexcept;
    DivertHandle& operator=(DivertHandle&& other) noexcept;

    /// Opens a filter handle. Returns std::nullopt and fills `error` on failure.
    [[nodiscard]] static std::optional<DivertHandle> open(
        std::string_view filter,
        WINDIVERT_LAYER layer,
        std::int16_t priority,
        std::uint64_t flags,
        std::string& error);

    [[nodiscard]] bool valid() const noexcept;
    explicit operator bool() const noexcept { return valid(); }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

    /// Blocks until a packet arrives. Returns false when the handle was shut down.
    [[nodiscard]] bool receive(PacketBuffer& packet) const noexcept;

    [[nodiscard]] bool send(const std::byte* data,
                            UINT length,
                            const WINDIVERT_ADDRESS& address) const noexcept;

    void shutdown() noexcept;
    void close() noexcept;

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

/// Returns a human readable description of the last Win32 error.
[[nodiscard]] std::string last_error_message();

/// Formats a specific Win32 error code.
[[nodiscard]] std::string format_error_message(DWORD error);

/// Prints a WinDivert specific hint for common open failures.
[[nodiscard]] std::string divert_error_hint(DWORD error);

} // namespace sdp::win
