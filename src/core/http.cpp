#include "shadowdpi/http.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string_view>

#include "shadowdpi/types.hpp"

namespace sdp {
namespace {

constexpr std::string_view kHttp10Prefix = "HTTP/1.0 ";
constexpr std::string_view kHttp11Prefix = "HTTP/1.1 ";
constexpr std::string_view kHostReplacement = "\r\nhoSt: ";
constexpr std::string_view kLocationHttp = "\r\nLocation: http://";
constexpr std::string_view kConnectionClose = "\r\nConnection: close";

constexpr std::array<std::string_view, 9> kHttpMethods = {
    "GET ", "HEAD ", "POST ", "PUT ", "DELETE ", "CONNECT ", "OPTIONS ",
    "PATCH ", "TRACE ",
};

[[nodiscard]] unsigned char fold_ascii(unsigned char value) noexcept
{
    if (value >= 'A' && value <= 'Z') {
        return static_cast<unsigned char>(value - 'A' + 'a');
    }
    return value;
}

[[nodiscard]] bool byte_matches(std::byte value,
                                char character,
                                bool case_insensitive) noexcept
{
    const auto left = std::to_integer<unsigned char>(value);
    const auto right = static_cast<unsigned char>(character);
    if (case_insensitive) {
        return fold_ascii(left) == fold_ascii(right);
    }
    return left == right;
}

[[nodiscard]] std::optional<std::size_t> find_bytes(
    std::span<const std::byte> haystack,
    std::string_view needle,
    bool case_insensitive = false)
{
    if (needle.empty() || haystack.size() < needle.size()) {
        return std::nullopt;
    }

    const auto iterator = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [case_insensitive](std::byte value, char character) {
            return byte_matches(value, character, case_insensitive);
        });

    if (iterator == haystack.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(iterator - haystack.begin());
}

[[nodiscard]] bool starts_with_text(std::span<const std::byte> haystack,
                                    std::string_view needle) noexcept
{
    if (haystack.size() < needle.size()) {
        return false;
    }
    for (std::size_t index = 0; index < needle.size(); ++index) {
        if (!byte_matches(haystack[index], needle[index], true)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<HeaderLocation> find_header(
    std::span<const std::byte> payload,
    std::string_view header_name)
{
    // Header names are case-insensitive and only the header block may be
    // inspected; a "\r\nHost: " sequence in a request body is not a header.
    const auto header_end = find_bytes(payload, "\r\n\r\n");
    const std::size_t search_length =
        header_end ? std::min(payload.size(), *header_end + 4)
                   : payload.size();
    const auto header_block = payload.first(search_length);

    const auto name_offset = find_bytes(header_block, header_name, true);
    if (!name_offset) {
        return std::nullopt;
    }

    const std::size_t value_offset = *name_offset + header_name.size();
    if (value_offset > header_block.size()) {
        return std::nullopt;
    }

    const auto line_end =
        find_bytes(header_block.subspan(value_offset), "\r\n");
    const std::size_t value_length =
        line_end ? *line_end : header_block.size() - value_offset;
    if (value_length == 0) {
        return std::nullopt;
    }

    // Only Host is capped at DNS name length; User-Agent may be longer.
    if (header_name == "\r\nHost: " &&
        (value_length < 3 || value_length > kMaxHostLength)) {
        return std::nullopt;
    }

    return HeaderLocation{*name_offset, value_offset, value_length};
}

bool is_passive_dpi_redirect(std::span<const std::byte> payload)
{
    if (!starts_with_text(payload, kHttp10Prefix) &&
        !starts_with_text(payload, kHttp11Prefix)) {
        return false;
    }
    if (payload.size() < 12) {
        return false;
    }

    const auto digit = [](std::byte value) -> int {
        const auto character = std::to_integer<unsigned char>(value);
        return character >= '0' && character <= '9' ? character - '0' : -1;
    };
    const int hundreds = digit(payload[9]);
    const int tens = digit(payload[10]);
    const int ones = digit(payload[11]);
    if (hundreds < 0 || tens < 0 || ones < 0) {
        return false;
    }
    const int status = hundreds * 100 + tens * 10 + ones;
    if (status != 301 && status != 302) {
        return false;
    }

    return find_bytes(payload, kLocationHttp, true).has_value() &&
           find_bytes(payload, kConnectionClose, true).has_value();
}

std::optional<std::size_t> find_http_method_end(
    std::span<const std::byte> payload,
    unsigned int fragment_size,
    bool* fragmented)
{
    for (const auto method : kHttpMethods) {
        if (payload.size() >= method.size()) {
            bool matches = true;
            for (std::size_t index = 0; index < method.size(); ++index) {
                if (!byte_matches(payload[index], method[index], false)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                if (fragmented) {
                    *fragmented = false;
                }
                return method.size() - 1;
            }
        }

        if ((fragment_size == 1 || fragment_size == 2) &&
            payload.size() >= method.size() - fragment_size) {
            const auto suffix = method.substr(fragment_size);
            bool matches = true;
            for (std::size_t index = 0; index < suffix.size(); ++index) {
                if (!byte_matches(payload[index], suffix[index], false)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                if (fragmented) {
                    *fragmented = true;
                }
                return method.size() - fragment_size - 1;
            }
        }
    }
    return std::nullopt;
}

void mix_case(std::span<std::byte> value)
{
    for (std::size_t index = 1; index < value.size(); index += 2) {
        const auto character = static_cast<unsigned char>(
            std::to_integer<unsigned char>(value[index]));
        if (character >= 'a' && character <= 'z') {
            value[index] = static_cast<std::byte>(character - 'a' + 'A');
        }
    }
}

void replace_host_name(std::span<std::byte> payload, const HeaderLocation& host)
{
    if (host.name_offset + kHostReplacement.size() > payload.size()) {
        return;
    }
    for (std::size_t index = 0; index < kHostReplacement.size(); ++index) {
        payload[host.name_offset + index] =
            static_cast<std::byte>(kHostReplacement[index]);
    }
}

bool move_host_space_to_user_agent(
    std::span<std::byte> payload,
    const HeaderLocation& host,
    const HeaderLocation& user_agent)
{
    const std::size_t host_end = host.value_offset + host.value_length;
    const std::size_t user_agent_end =
        user_agent.value_offset + user_agent.value_length;
    if (host_end > payload.size() || user_agent_end > payload.size()) {
        return false;
    }

    if (user_agent.value_offset > host.value_offset) {
        if (host.value_offset == 0 ||
            user_agent_end <= host.value_offset) {
            return false;
        }
        std::memmove(payload.data() + host.value_offset - 1,
                     payload.data() + host.value_offset,
                     user_agent_end - host.value_offset);
        payload[user_agent_end - 1] = std::byte{' '};
        return true;
    }

    if (host.value_offset <= 1 || user_agent_end > host.value_offset - 1) {
        return false;
    }
    const std::size_t length = host.value_offset - 1 - user_agent_end;
    std::memmove(payload.data() + user_agent_end + 1,
                 payload.data() + user_agent_end,
                 length);
    payload[user_agent_end] = std::byte{' '};
    return true;
}

bool add_extra_space_before_uri(
    std::span<std::byte> payload,
    std::size_t method_end,
    std::size_t host_value_offset)
{
    if (method_end >= host_value_offset ||
        host_value_offset > payload.size()) {
        return false;
    }

    const std::size_t length = host_value_offset - method_end - 1;
    if (length == 0) {
        return false;
    }
    std::memmove(payload.data() + method_end + 1,
                 payload.data() + method_end,
                 length);
    return true;
}

} // namespace sdp
