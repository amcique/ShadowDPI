#include "shadowdpi/tls.hpp"

#include <cstdint>

#include "shadowdpi/types.hpp"

namespace sdp {
namespace {

[[nodiscard]] unsigned char byte_value(std::byte value) noexcept
{
    return std::to_integer<unsigned char>(value);
}

} // namespace

bool is_tls_client_hello_prefix(std::span<const std::byte> payload) noexcept
{
    if (payload.size() == 2) {
        return byte_value(payload[0]) == 0x16 &&
               byte_value(payload[1]) == 0x03;
    }
    if (payload.size() >= 3) {
        const auto version = byte_value(payload[2]);
        return byte_value(payload[0]) == 0x16 &&
               byte_value(payload[1]) == 0x03 &&
               (version == 0x01 || version == 0x03);
    }
    return false;
}

std::optional<SniLocation> extract_sni(std::span<const std::byte> payload)
{
    const auto* data =
        reinterpret_cast<const unsigned char*>(payload.data());
    const std::size_t size = payload.size();

    for (std::size_t pointer = 0; pointer + 8 < size; ++pointer) {
        if (data[pointer] != 0 || data[pointer + 1] != 0 ||
            data[pointer + 2] != 0 || data[pointer + 4] != 0 ||
            data[pointer + 6] != 0 || data[pointer + 7] != 0) {
            continue;
        }

        if (static_cast<int>(data[pointer + 3]) -
                static_cast<int>(data[pointer + 5]) != 2 ||
            static_cast<int>(data[pointer + 5]) -
                static_cast<int>(data[pointer + 8]) != 3) {
            continue;
        }

        const std::size_t hostname_length = data[pointer + 8];
        if (hostname_length < 3 || hostname_length > kMaxHostLength) {
            return std::nullopt;
        }

        const std::size_t hostname_offset = pointer + 9;
        if (hostname_offset + hostname_length > size) {
            return std::nullopt;
        }

        for (std::size_t index = 0; index < hostname_length; ++index) {
            const unsigned char character = data[hostname_offset + index];
            const bool valid =
                (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                character == '.' || character == '-';
            if (!valid) {
                return std::nullopt;
            }
        }

        return SniLocation{
            std::string_view{
                reinterpret_cast<const char*>(data + hostname_offset),
                hostname_length},
            hostname_offset};
    }

    return std::nullopt;
}

} // namespace sdp
