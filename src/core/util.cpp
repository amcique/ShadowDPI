#include "shadowdpi/util.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <system_error>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace sdp {
namespace {

int hex_digit(char value) noexcept
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

} // namespace

std::string replace_all(std::string input,
                        std::string_view from,
                        std::string_view to)
{
    if (from.empty()) {
        return input;
    }

    std::size_t position = 0;
    while ((position = input.find(from, position)) != std::string::npos) {
        input.replace(position, from.size(), to);
        position += to.size();
    }
    return input;
}

std::uint16_t read_be16(const std::byte* data) noexcept
{
    return static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(data[0]) << 8) |
        std::to_integer<std::uint16_t>(data[1]));
}

std::uint32_t read_be32(const std::byte* data) noexcept
{
    return (std::to_integer<std::uint32_t>(data[0]) << 24) |
           (std::to_integer<std::uint32_t>(data[1]) << 16) |
           (std::to_integer<std::uint32_t>(data[2]) << 8) |
           std::to_integer<std::uint32_t>(data[3]);
}

void write_be16(std::byte* data, std::uint16_t value) noexcept
{
    data[0] = static_cast<std::byte>((value >> 8) & 0xFF);
    data[1] = static_cast<std::byte>(value & 0xFF);
}

std::string to_ascii_lower(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte >= 'A' && byte <= 'Z') {
            result.push_back(static_cast<char>(byte - 'A' + 'a'));
        }
        else {
            result.push_back(character);
        }
    }
    return result;
}

bool ascii_iequals(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        auto left = static_cast<unsigned char>(lhs[index]);
        auto right = static_cast<unsigned char>(rhs[index]);
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<unsigned char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<unsigned char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

std::optional<IpAddress> parse_ip_address(std::string_view text)
{
    IpAddress result;
    const std::string terminated{text};

    if (text.find(':') != std::string_view::npos) {
        IN6_ADDR address{};
        if (InetPtonA(AF_INET6, terminated.c_str(), &address) != 1) {
            return std::nullopt;
        }
        result.family = IpFamily::v6;
        std::memcpy(result.bytes.data(), &address, sizeof(address));
        return result;
    }

    IN_ADDR address{};
    if (InetPtonA(AF_INET, terminated.c_str(), &address) != 1) {
        return std::nullopt;
    }
    result.family = IpFamily::v4;
    std::memcpy(result.bytes.data(), &address, sizeof(address));
    return result;
}

std::string to_string(const IpAddress& address)
{
    char buffer[INET6_ADDRSTRLEN]{};
    const int family = address.family == IpFamily::v4 ? AF_INET : AF_INET6;
    if (InetNtopA(family, address.bytes.data(), buffer,
                  static_cast<DWORD>(std::size(buffer))) == nullptr) {
        return "<invalid>";
    }
    return buffer;
}

std::optional<std::vector<std::byte>> parse_hex_payload(std::string_view text)
{
    if (text.empty() || (text.size() % 2) != 0 ||
        text.size() > kMaxFakePayloadSize * 2) {
        return std::nullopt;
    }

    std::vector<std::byte> result(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        const int high = hex_digit(text[index]);
        const int low = hex_digit(text[index + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        result[index / 2] = static_cast<std::byte>((high << 4) | low);
    }
    return result;
}

} // namespace sdp
