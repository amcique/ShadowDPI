#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "shadowdpi/ipc.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sddl.h>

namespace sdp::ipc {

/// Builds a DACL granting full access to SYSTEM, Administrators and, when
/// supplied, one additional user SID. Everyone else is denied implicitly.
[[nodiscard]] std::string build_pipe_sddl(
    const std::optional<std::string>& allowed_sid) noexcept;

[[nodiscard]] std::string pipe_name(std::string_view base);

struct Caller {
    bool authenticated{false};
    bool admin{false};
    bool config_writer{false};
};

class IpcServer {
public:
    using Handler = std::function<Response(const Request&, const Caller&)>;

    IpcServer(std::string name,
              std::string sddl,
              std::optional<std::string> config_writer_sid);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    void set_handler(Handler handler);
    [[nodiscard]] bool start(std::string& error) noexcept;
    void stop() noexcept;

private:
    [[nodiscard]] HANDLE create_pipe() noexcept;
    void run() noexcept;
    void serve_client() noexcept;

    std::string name_;
    std::string sddl_;
    std::optional<std::string> config_writer_sid_;
    HANDLE pipe_{INVALID_HANDLE_VALUE};
    std::atomic<bool> stop_{false};
    std::thread thread_;
    Handler handler_;
    std::mutex handler_mutex_;
};

class IpcClient {
public:
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;
    IpcClient(IpcClient&& other) noexcept;
    IpcClient& operator=(IpcClient&& other) noexcept;
    ~IpcClient();

    [[nodiscard]] static std::optional<IpcClient> connect(
        const std::string& name,
        std::string& error) noexcept;

    [[nodiscard]] std::optional<Response> round_trip(
        const Request& request,
        MessageType& type,
        std::string& error) noexcept;

private:
    explicit IpcClient(HANDLE handle) noexcept;

    HANDLE handle_{INVALID_HANDLE_VALUE};
};

} // namespace sdp::ipc
