#include "ipc_pipe.hpp"

#include <chrono>
#include <cstdint>
#include <thread>

namespace sdp::ipc {
namespace {

constexpr DWORD kMaxFrame = static_cast<DWORD>(kIpcMaxPayload + 64U);

[[nodiscard]] bool valid_sid(std::string_view sid) noexcept
{
    if (sid.size() < 4 || sid.size() > 184 || sid[0] != 'S' ||
        sid[1] != '-') {
        return false;
    }
    for (std::size_t index = 2; index < sid.size(); ++index) {
        const char character = sid[index];
        if (character != '-' && (character < '0' || character > '9')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_exact(HANDLE handle, void* data, DWORD length) noexcept
{
    auto* bytes = static_cast<std::byte*>(data);
    DWORD received = 0;
    for (int idle = 0; idle < 500 && received < length; ++idle) {
        DWORD current = 0;
        if (!ReadFile(handle, bytes + received, length - received, &current,
                      nullptr)) {
            if (GetLastError() == ERROR_NO_DATA) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            return false;
        }
        if (current == 0) {
            return false;
        }
        received += current;
    }
    return received == length;
}

[[nodiscard]] bool write_all(HANDLE handle,
                             const void* data,
                             DWORD length) noexcept
{
    const auto* bytes = static_cast<const std::byte*>(data);
    DWORD written = 0;
    while (written < length) {
        DWORD current = 0;
        if (!WriteFile(handle, bytes + written, length - written, &current,
                       nullptr)) {
            return false;
        }
        if (current == 0) {
            return false;
        }
        written += current;
    }
    return true;
}

[[nodiscard]] DWORD frame_length(std::uint32_t value) noexcept
{
    return value <= kMaxFrame ? static_cast<DWORD>(value) : 0;
}

[[nodiscard]] std::vector<std::byte> read_frame(HANDLE handle) noexcept
{
    std::uint32_t raw_length = 0;
    if (!read_exact(handle, &raw_length, sizeof(raw_length))) {
        return {};
    }
    const DWORD length = frame_length(raw_length);
    if (length == 0) {
        return {};
    }
    std::vector<std::byte> data(length);
    if (!read_exact(handle, data.data(), length)) {
        return {};
    }
    return data;
}

[[nodiscard]] bool write_frame(HANDLE handle,
                               std::span<const std::byte> data) noexcept
{
    if (data.empty() || data.size() > kMaxFrame) {
        return false;
    }
    const auto length = static_cast<std::uint32_t>(data.size());
    return write_all(handle, &length, sizeof(length)) &&
           write_all(handle, data.data(), static_cast<DWORD>(data.size()));
}

} // namespace

std::string build_pipe_sddl(
    const std::optional<std::string>& allowed_sid) noexcept
{
    std::string sddl = "D:P(A;;GA;;;SY)(A;;GA;;;BA)";
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

IpcServer::IpcServer(std::string name, std::string sddl)
    : name_(pipe_name(name))
    , sddl_(std::move(sddl))
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
    const std::vector<std::byte> frame = read_frame(pipe_);
    if (frame.empty()) {
        return;
    }
    const auto request = decode_request(frame);
    if (!request) {
        return;
    }

    Response response;
    MessageType response_type = MessageType::Error;
    Handler handler;
    {
        std::lock_guard lock{handler_mutex_};
        handler = handler_;
    }
    if (handler) {
        response = handler(*request);
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
        (void)read_exact(pipe_, &acknowledgment, 1);
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
    for (int attempt = 0; attempt < 20; ++attempt) {
        handle = CreateFileA(target.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                             nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
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
    if (encoded.empty() || !write_frame(handle_, encoded)) {
        error = "cannot write IPC request";
        return std::nullopt;
    }
    const std::vector<std::byte> frame = read_frame(handle_);
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
