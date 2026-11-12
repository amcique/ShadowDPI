#pragma once

#include <atomic>
#include <functional>
#include <mutex>

namespace sdp::win {

/// Coordinates shutdown between the packet loop, the console control handler
/// and the Windows service control handler.
class ShutdownCoordinator {
public:
    static ShutdownCoordinator& instance();

    ShutdownCoordinator(const ShutdownCoordinator&) = delete;
    ShutdownCoordinator& operator=(const ShutdownCoordinator&) = delete;

    /// Registers the callback that shuts down WinDivert handles.
    void set_stop_callback(std::function<void()> callback);

    /// Requests shutdown. Safe to call from any system callback thread.
    void request_stop() noexcept;

    [[nodiscard]] bool stop_requested() const noexcept;

private:
    ShutdownCoordinator() = default;

    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::function<void()> callback_;
};

/// Installs a console control handler that requests shutdown on Ctrl+C,
/// Ctrl+Break and window close.
void install_console_stop_handler() noexcept;

} // namespace sdp::win
