#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sdp::ipc {

inline constexpr std::uint32_t kIpcMagic = 0x53445049; // "SDPI"
inline constexpr std::uint16_t kIpcVersion = 1;
inline constexpr std::size_t kIpcMaxPayload = 64U * 1024U;

enum class MessageType : std::uint16_t {
    Handshake = 1,
    GetStatus,
    ApplyConfig,
    Start,
    Stop,
    RunScan,
    GetLog,
    Response,
    Error,
};

struct Request {
    std::uint32_t pid{0};
    MessageType type{MessageType::Handshake};
    std::vector<std::byte> payload;
};

struct Response {
    bool ok{false};
    std::string message;
};

[[nodiscard]] std::vector<std::byte> encode_request(const Request& request);
[[nodiscard]] std::optional<Request> decode_request(
    std::span<const std::byte> data);

[[nodiscard]] std::vector<std::byte> encode_response(const Response& response,
                                                     MessageType type);
[[nodiscard]] std::optional<Response> decode_response(
    std::span<const std::byte> data,
    MessageType& type_out);

[[nodiscard]] std::vector<std::byte> encode_argv(
    const std::vector<std::string>& arguments);
[[nodiscard]] std::optional<std::vector<std::string>> decode_argv(
    std::span<const std::byte> data);

} // namespace sdp::ipc
