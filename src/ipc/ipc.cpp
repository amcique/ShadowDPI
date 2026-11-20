#include "shadowdpi/ipc.hpp"

#include <limits>

namespace sdp::ipc {
namespace {

constexpr std::size_t kRequestHeaderSize = 16;
constexpr std::size_t kResponseHeaderSize = 13;

void put_u16(std::vector<std::byte>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::byte>(value & 0xFFU));
    out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void put_u32(std::vector<std::byte>& out, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] bool take_u16(std::span<const std::byte> data,
                            std::size_t& offset,
                            std::uint16_t& value) noexcept
{
    if (data.size() - offset < 2) {
        return false;
    }
    value = static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(data[offset]) |
        (std::to_integer<std::uint16_t>(data[offset + 1]) << 8U));
    offset += 2;
    return true;
}

[[nodiscard]] bool take_u32(std::span<const std::byte> data,
                            std::size_t& offset,
                            std::uint32_t& value) noexcept
{
    if (data.size() - offset < 4) {
        return false;
    }
    value = 0;
    for (unsigned int index = 0; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(data[offset + index]))
                 << (index * 8U);
    }
    offset += 4;
    return true;
}

[[nodiscard]] bool read_header(std::span<const std::byte> data,
                               std::size_t& offset,
                               MessageType& type) noexcept
{
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t raw_type = 0;
    if (!take_u32(data, offset, magic) || !take_u16(data, offset, version) ||
        !take_u16(data, offset, raw_type)) {
        return false;
    }
    if (magic != kIpcMagic || version != kIpcVersion ||
        raw_type < static_cast<std::uint16_t>(MessageType::Handshake) ||
        raw_type > static_cast<std::uint16_t>(kLastMessage)) {
        return false;
    }
    type = static_cast<MessageType>(raw_type);
    return true;
}

} // namespace

bool requires_privilege(MessageType type) noexcept
{
    switch (type) {
    case MessageType::ApplyConfig:
    case MessageType::Start:
    case MessageType::Stop:
    case MessageType::RunScan:
    case MessageType::WriteBlacklist:
        return true;
    default:
        return false;
    }
}

bool caller_may_run(const Caller& caller, MessageType type) noexcept
{
    if (!caller.authenticated) {
        return false;
    }
    return !requires_privilege(type) || caller.admin ||
           caller.config_writer;
}

std::vector<std::byte> encode_request(const Request& request)
{
    if (request.payload.size() > kIpcMaxPayload) {
        return {};
    }

    std::vector<std::byte> out;
    out.reserve(kRequestHeaderSize + request.payload.size());
    put_u32(out, kIpcMagic);
    put_u16(out, kIpcVersion);
    put_u16(out, static_cast<std::uint16_t>(request.type));
    put_u32(out, request.pid);
    put_u32(out, static_cast<std::uint32_t>(request.payload.size()));
    out.insert(out.end(), request.payload.begin(), request.payload.end());
    return out;
}

std::optional<Request> decode_request(std::span<const std::byte> data)
{
    std::size_t offset = 0;
    MessageType type{};
    if (!read_header(data, offset, type)) {
        return std::nullopt;
    }

    std::uint32_t pid = 0;
    std::uint32_t payload_length = 0;
    if (!take_u32(data, offset, pid) ||
        !take_u32(data, offset, payload_length)) {
        return std::nullopt;
    }
    if (payload_length > kIpcMaxPayload ||
        data.size() - offset != payload_length) {
        return std::nullopt;
    }

    Request request;
    request.pid = pid;
    request.type = type;
    request.payload.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                           data.end());
    return request;
}

std::vector<std::byte> encode_response(const Response& response,
                                       MessageType type)
{
    if (response.message.size() > kIpcMaxPayload) {
        return {};
    }

    std::vector<std::byte> out;
    out.reserve(kResponseHeaderSize + response.message.size());
    put_u32(out, kIpcMagic);
    put_u16(out, kIpcVersion);
    put_u16(out, static_cast<std::uint16_t>(type));
    out.push_back(response.ok ? std::byte{1} : std::byte{0});
    put_u32(out, static_cast<std::uint32_t>(response.message.size()));
    for (const char character : response.message) {
        out.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    return out;
}

std::optional<Response> decode_response(std::span<const std::byte> data,
                                        MessageType& type_out)
{
    std::size_t offset = 0;
    MessageType type{};
    if (!read_header(data, offset, type)) {
        return std::nullopt;
    }
    if (type != MessageType::Response && type != MessageType::Error) {
        return std::nullopt;
    }
    if (data.size() - offset < 1) {
        return std::nullopt;
    }
    const auto ok_byte = std::to_integer<std::uint8_t>(data[offset++]);
    if (ok_byte > 1) {
        return std::nullopt;
    }

    std::uint32_t message_length = 0;
    if (!take_u32(data, offset, message_length) ||
        message_length > kIpcMaxPayload ||
        data.size() - offset != message_length) {
        return std::nullopt;
    }

    Response response;
    response.ok = ok_byte == 1;
    response.message.reserve(message_length);
    for (std::size_t index = offset; index < data.size(); ++index) {
        response.message.push_back(static_cast<char>(
            std::to_integer<unsigned char>(data[index])));
    }
    type_out = type;
    return response;
}

std::vector<std::byte> encode_argv(const std::vector<std::string>& arguments)
{
    std::vector<std::byte> out;
    for (const auto& argument : arguments) {
        for (const char character : argument) {
            out.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
        out.push_back(std::byte{0});
    }
    return out;
}

std::optional<std::vector<std::string>> decode_argv(
    std::span<const std::byte> data)
{
    if (data.empty()) {
        return std::vector<std::string>{};
    }
    if (std::to_integer<std::uint8_t>(data.back()) != 0) {
        return std::nullopt;
    }

    std::vector<std::string> arguments;
    std::string current;
    for (const auto byte : data) {
        const auto value = std::to_integer<unsigned char>(byte);
        if (value == 0) {
            arguments.push_back(current);
            current.clear();
        }
        else {
            current.push_back(static_cast<char>(value));
        }
    }
    return arguments;
}

} // namespace sdp::ipc
