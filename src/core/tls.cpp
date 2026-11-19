#include "shadowdpi/tls.hpp"

#include <cstdint>
#include <string_view>

#include "shadowdpi/types.hpp"

namespace sdp {
namespace {

[[nodiscard]] unsigned char byte_value(std::byte value) noexcept
{
    return std::to_integer<unsigned char>(value);
}

[[nodiscard]] std::uint16_t read_be16(const unsigned char* data) noexcept
{
    return static_cast<std::uint16_t>((data[0] << 8U) | data[1]);
}

[[nodiscard]] std::uint32_t read_be24(const unsigned char* data) noexcept
{
    return (static_cast<std::uint32_t>(data[0]) << 16U) |
           (static_cast<std::uint32_t>(data[1]) << 8U) | data[2];
}

[[nodiscard]] bool valid_hostname(std::string_view name) noexcept
{
    if (name.size() < 3 || name.size() > kMaxHostLength) {
        return false;
    }
    for (const char character : name) {
        const auto value = static_cast<unsigned char>(character);
        const bool valid =
            (value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            value == '.' || value == '-';
        if (!valid) {
            return false;
        }
    }
    return true;
}

} // namespace

bool is_tls_client_hello_prefix(std::span<const std::byte> payload) noexcept
{
    if (payload.size() == 2) {
        return byte_value(payload[0]) == 0x16 &&
               byte_value(payload[1]) == 0x03;
    }
    if (payload.size() >= 3) {
        return byte_value(payload[0]) == 0x16 &&
               byte_value(payload[1]) == 0x03 &&
               byte_value(payload[2]) <= 0x03;
    }
    return false;
}

std::optional<SniLocation> extract_sni(std::span<const std::byte> payload)
{
    const auto* data = reinterpret_cast<const unsigned char*>(payload.data());
    const std::size_t size = payload.size();

    if (size < 5 || data[0] != 0x16 || data[1] != 0x03) {
        return std::nullopt;
    }
    const std::size_t record_end = 5U + read_be16(data + 3);
    if (record_end < 9 || record_end > size || data[5] != 0x01) {
        return std::nullopt;
    }

    const std::size_t handshake_end = 9U + read_be24(data + 6);
    if (handshake_end > record_end ||
        handshake_end < 9U + 2U + 32U + 1U) {
        return std::nullopt;
    }

    std::size_t offset = 9U + 2U + 32U; // version and random
    const std::size_t session_id_length = data[offset++];
    if (session_id_length > 32 ||
        session_id_length > handshake_end - offset) {
        return std::nullopt;
    }
    offset += session_id_length;

    if (handshake_end - offset < 2) {
        return std::nullopt;
    }
    const std::size_t cipher_length = read_be16(data + offset);
    offset += 2;
    if ((cipher_length & 1U) != 0 || cipher_length > handshake_end - offset) {
        return std::nullopt;
    }
    offset += cipher_length;

    if (handshake_end - offset < 1) {
        return std::nullopt;
    }
    const std::size_t compression_length = data[offset++];
    if (compression_length > handshake_end - offset) {
        return std::nullopt;
    }
    offset += compression_length;

    if (handshake_end - offset < 2) {
        return std::nullopt;
    }
    const std::size_t extensions_length = read_be16(data + offset);
    offset += 2;
    if (extensions_length > handshake_end - offset) {
        return std::nullopt;
    }
    const std::size_t extensions_end = offset + extensions_length;

    while (extensions_end - offset >= 4) {
        const std::uint16_t type = read_be16(data + offset);
        const std::size_t extension_length = read_be16(data + offset + 2);
        offset += 4;
        if (extension_length > extensions_end - offset) {
            return std::nullopt;
        }
        if (type == 0x0000) {
            if (extension_length < 5) {
                return std::nullopt;
            }
            std::size_t name_offset = offset + 2;
            const std::size_t list_length = read_be16(data + offset);
            if (list_length < 3 || list_length > extension_length - 2) {
                return std::nullopt;
            }
            const std::size_t list_end = name_offset + list_length;
            while (list_end - name_offset >= 3) {
                const unsigned char name_type = data[name_offset++];
                const std::size_t host_length = read_be16(data + name_offset);
                name_offset += 2;
                if (host_length > list_end - name_offset) {
                    return std::nullopt;
                }
                if (name_type == 0 && valid_hostname(
                        std::string_view{
                            reinterpret_cast<const char*>(data + name_offset),
                            host_length})) {
                    return SniLocation{
                        std::string_view{
                            reinterpret_cast<const char*>(data + name_offset),
                            host_length},
                        name_offset};
                }
                name_offset += host_length;
            }
            return std::nullopt;
        }
        offset += extension_length;
    }

    return std::nullopt;
}

} // namespace sdp
