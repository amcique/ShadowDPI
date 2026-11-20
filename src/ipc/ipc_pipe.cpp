#include "ipc_pipe.hpp"

#include <chrono>
#include <cstdint>
#include <thread>

namespace sdp::ipc {
namespace {

constexpr DWORD kMaxFrame = static_cast<DWORD>(kIpcMaxPayload + 64U);

[[nodiscard]] bool valid_sid(std::string_view sid) noexcept
{
    if (sid.empty() || sid.size() > 184) {
        return false;
    }
    std::string text{sid};
    PSID converted = nullptr;
    if (!ConvertStringSidToSidA(text.c_str(), &converted)) {
        return false;
    }
    const bool valid = IsValidSid(converted) != FALSE;
    LocalFree(converted);
    return valid;
}

[[nodiscard]] bool read_exact(HANDLE handle,
                              void* data,
                              DWORD length,
                              const std::atomic<bool>* stop = nullptr,
                              int max_idle = 100) noexcept
{
    auto* bytes = static_cast<std::byte*>(data);
    DWORD received = 0;
    int idle = 0;
    while (received < length) {
        if (stop != nullptr && stop->load(std::memory_order_acquire)) {
            return false;
        }
        DWORD current = 0;
        if (!ReadFile(handle, bytes + received, length - received, &current,
                      nullptr)) {
            if (GetLastError() == ERROR_NO_DATA && ++idle <= max_idle) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            return false;
        }
        if (current == 0) {
            return false;
        }
        received += current;
        idle = 0;
    }
    return true;
}

[[nodiscard]] bool write_all(HANDLE handle,
                             const void* data,
                             DWORD length,
                             int max_idle = 500) noexcept
{
    const auto* bytes = static_cast<const std::byte*>(data);
    DWORD written = 0;
    int idle = 0;
    while (written < length) {
        DWORD current = 0;
        if (!WriteFile(handle, bytes + written, length - written, &current,
                       nullptr)) {
            if (GetLastError() == ERROR_NO_DATA && ++idle <= max_idle) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            return false;
        }
        if (current == 0) {
            return false;
        }
        written += current;
        idle = 0;
    }
    return true;
}

[[nodiscard]] DWORD frame_length(std::uint32_t value) noexcept
{
    return value <= kMaxFrame ? static_cast<DWORD>(value) : 0;
}

[[nodiscard]] std::vector<std::byte> read_frame(
    HANDLE handle,
    const std::atomic<bool>* stop = nullptr,
    int max_idle = 100) noexcept
{
    std::uint32_t raw_length = 0;
    if (!read_exact(handle, &raw_length, sizeof(raw_length), stop, max_idle)) {
        return {};
    }
    const DWORD length = frame_length(raw_length);
    if (length == 0) {
        return {};
    }
    std::vector<std::byte> data(length);
    if (!read_exact(handle, data.data(), length, stop, max_idle)) {
        return {};
    }
    return data;
}

[[nodiscard]] Caller identify_caller(
    HANDLE pipe,
    const std::optional<std::string>& config_writer_sid) noexcept
{
    Caller caller;
    if (!ImpersonateNamedPipeClient(pipe)) {
        return caller;
    }
    caller.authenticated = true;

    PSID administrators = nullptr;
    if (ConvertStringSidToSidA("S-1-5-32-544", &administrators)) {
        BOOL member = FALSE;
        if (CheckTokenMembership(nullptr, administrators, &member)) {
            caller.admin = member != FALSE;
        }
        FreeSid(administrators);
    }

    if (config_writer_sid) {
        PSID writer = nullptr;
        if (ConvertStringSidToSidA(config_writer_sid->c_str(), &writer)) {
            BOOL member = FALSE;
            if (CheckTokenMembership(nullptr, writer, &member)) {
                caller.config_writer = member != FALSE;
            }
            FreeSid(writer);
        }
    }

    RevertToSelf();
    return caller;
}

[[nodiscard]] bool write_frame(HANDLE handle,
                               std::span<const std::byte> data,
                               int max_idle = 100) noexcept
{
    if (data.empty() || data.size() > kMaxFrame) {
        return false;
    }
    const auto length = static_cast<std::uint32_t>(data.size());
    return write_all(handle, &length, sizeof(length), max_idle) &&
           write_all(handle, data.data(), static_cast<DWORD>(data.size()),
                     max_idle);
}

} // namespace

std::string build_pipe_sddl(
    const std::optional<std::string>& allowed_sid) noexcept
{
    // Authenticated users may connect and read status. Privileged commands
    // are authorized separately after impersonating the caller.
    std::string sddl = "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)";
    if (allowed_sid) {
        if (!valid_sid(*allowed_sid)) {
            return {};
        }
        sddl += "(A;;GA;;;" + *allowed_sid + ")";
    }
    return sddl;
}

std::string pipe_name(std::string_view base)
{
    return "\\\\.\\pipe\\" + std::string(base);
}

IpcServer::IpcServer(std::string name,
                     std::string sddl,
                     std::optional<std::string> config_writer_sid)
    : name_(pipe_name(name))
    , sddl_(std::move(sddl))
    , config_writer_sid_(std::move(config_writer_sid))
{
}

IpcServer::~IpcServer()
{
    stop();
}

void IpcServer::set_handler(Handler handler)
{
    std::lock_guard lock{handler_mutex_};
    handler_ = std::move(handler);
}

HANDLE IpcServer::create_pipe() noexcept
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            sddl_.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        return INVALID_HANDLE_VALUE;
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;

    HANDLE handle = CreateNamedPipeA(
        name_.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
        PIPE_UNLIMITED_INSTANCES, kMaxFrame, kMaxFrame, 0, &attributes);
    LocalFree(descriptor);
    return handle;
}

bool IpcServer::start(std::string& error) noexcept
{
    if (sddl_.empty()) {
        error = "invalid pipe security descriptor";
        return false;
    }
    HANDLE handle = create_pipe();
    if (handle == INVALID_HANDLE_VALUE) {
        error = "cannot create IPC pipe: " +
                std::to_string(GetLastError());
        return false;
    }
    pipe_ = handle;
    stop_.store(false, std::memory_order_release);
    try {
        thread_ = std::thread([this] { run(); });
    }
    catch (...) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        error = "cannot start IPC thread";
        return false;
    }
    return true;
}

void IpcServer::stop() noexcept
{
    stop_.store(true, std::memory_order_release);

    if (thread_.joinable()) {
        thread_.join();
    }
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

void IpcServer::serve_client() noexcept
{
    const std::vector<std::byte> frame = read_frame(pipe_, &stop_);
    if (frame.empty()) {
        return;
    }
    const auto request = decode_request(frame);
    if (!request) {
        return;
    }

    const Caller caller = identify_caller(pipe_, config_writer_sid_);
    Response response;
    MessageType response_type = MessageType::Error;
    Handler handler;
    {
        std::lock_guard lock{handler_mutex_};
        handler = handler_;
    }
    if (handler) {
        response = handler(*request, caller);
        if (response.ok) {
            response_type = MessageType::Response;
        }
    }
    else {
        response.message = "no handler registered";
    }
    if (write_frame(pipe_, encode_response(response, response_type))) {
        // Wait for the client to drain the reply. DisconnectNamedPipe discards
        // unread data, so leaving before this point can lose the response.
        std::byte acknowledgment{};
        (void)read_exact(pipe_, &acknowledgment, 1, &stop_, 20);
    }
}

void IpcServer::run() noexcept
{
    while (!stop_.load(std::memory_order_acquire)) {
        if (pipe_ == INVALID_HANDLE_VALUE) {
            pipe_ = create_pipe();
            if (pipe_ == INVALID_HANDLE_VALUE) {
                return;
            }
        }

        const BOOL connected = ConnectNamedPipe(pipe_, nullptr);
        const DWORD error = GetLastError();
        if (connected || error == ERROR_PIPE_LISTENING) {
            // No client yet in nonblocking mode. Poll again shortly.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (error != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (stop_.load(std::memory_order_acquire)) {
            DisconnectNamedPipe(pipe_);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
            return;
        }

        serve_client();
        DisconnectNamedPipe(pipe_);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

IpcClient::IpcClient(HANDLE handle) noexcept
    : handle_(handle)
{
}

IpcClient::IpcClient(IpcClient&& other) noexcept
    : handle_(other.handle_)
{
    other.handle_ = INVALID_HANDLE_VALUE;
}

IpcClient& IpcClient::operator=(IpcClient&& other) noexcept
{
    if (this != &other) {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = other.handle_;
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
}

IpcClient::~IpcClient()
{
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
    }
}

std::optional<IpcClient> IpcClient::connect(const std::string& name,
                                             std::string& error) noexcept
{
    const std::string target = pipe_name(name);
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 5; ++attempt) {
        handle = CreateFileA(target.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                             nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            if (SetNamedPipeHandleState(handle, &mode, nullptr, nullptr) ==
                FALSE) {
                CloseHandle(handle);
                error = "cannot set IPC client mode";
                return std::nullopt;
            }
            return IpcClient{handle};
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND &&
            GetLastError() != ERROR_PIPE_BUSY) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    error = "cannot connect to IPC pipe: " + std::to_string(GetLastError());
    return std::nullopt;
}

std::optional<Response> IpcClient::round_trip(const Request& request,
                                              MessageType& type,
                                              std::string& error) noexcept
{
    const std::vector<std::byte> encoded = encode_request(request);
    if (encoded.empty() ||
        !write_frame(handle_, encoded, 100)) {
        error = "cannot write IPC request";
        return std::nullopt;
    }
    const std::vector<std::byte> frame = read_frame(handle_, nullptr, 100);
    if (frame.empty()) {
        error = "cannot read IPC response";
        return std::nullopt;
    }
    const auto response = decode_response(frame, type);
    if (!response) {
        error = "malformed IPC response";
        return std::nullopt;
    }
    const std::byte acknowledgment{1};
    (void)write_all(handle_, &acknowledgment, 1);
    return response;
}

} // namespace sdp::ipc
