#include "lifecycle.hpp"

#include <utility>

#include "win_headers.hpp"

namespace sdp::win {
namespace {

BOOL WINAPI console_control_handler(DWORD control)
{
    switch (control) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        ShutdownCoordinator::instance().request_stop();
        return TRUE;
    default:
        return FALSE;
    }
}

} // namespace

ShutdownCoordinator& ShutdownCoordinator::instance()
{
    static ShutdownCoordinator coordinator;
    return coordinator;
}

void ShutdownCoordinator::set_stop_callback(std::function<void()> callback)
{
    std::lock_guard lock{mutex_};
    callback_ = std::move(callback);
}

void ShutdownCoordinator::reset() noexcept
{
    stop_.store(false, std::memory_order_release);
    std::lock_guard lock{mutex_};
    callback_ = {};
}

void ShutdownCoordinator::request_stop() noexcept
{
    stop_.store(true, std::memory_order_release);

    std::function<void()> callback;
    {
        std::lock_guard lock{mutex_};
        callback = callback_;
    }
    if (callback) {
        callback();
    }
}

bool ShutdownCoordinator::stop_requested() const noexcept
{
    return stop_.load(std::memory_order_acquire);
}

void install_console_stop_handler() noexcept
{
    SetConsoleCtrlHandler(console_control_handler, TRUE);
}

} // namespace sdp::win
