#include "config_store.hpp"

#include <fstream>
#include <sstream>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace sdp::ipc {
namespace {

constexpr std::string_view kPrefix = "{\"arguments\":\"";
constexpr std::string_view kSuffix = "\"}";

void append_escaped(std::string& out, unsigned char value)
{
    switch (value) {
    case '"':
        out += "\\\"";
        break;
    case '\\':
        out += "\\\\";
        break;
    case '\b':
        out += "\\b";
        break;
    case '\f':
        out += "\\f";
        break;
    case '\n':
        out += "\\n";
        break;
    case '\r':
        out += "\\r";
        break;
    case '\t':
        out += "\\t";
        break;
    default:
        if (value < 0x20) {
            static constexpr char kHex[] = "0123456789abcdef";
            out += "\\u00";
            out.push_back(kHex[value >> 4U]);
            out.push_back(kHex[value & 0x0FU]);
        }
        else {
            out.push_back(static_cast<char>(value));
        }
    }
}

[[nodiscard]] bool hex_value(char character, unsigned char& value) noexcept
{
    if (character >= '0' && character <= '9') {
        value = static_cast<unsigned char>(character - '0');
        return true;
    }
    if (character >= 'a' && character <= 'f') {
        value = static_cast<unsigned char>(character - 'a' + 10);
        return true;
    }
    if (character >= 'A' && character <= 'F') {
        value = static_cast<unsigned char>(character - 'A' + 10);
        return true;
    }
    return false;
}

[[nodiscard]] bool unescape(const std::string& source,
                            std::string& out) noexcept
{
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] != '\\') {
            out.push_back(source[index]);
            continue;
        }
        if (++index >= source.size()) {
            return false;
        }
        switch (source[index]) {
        case '"':
            out.push_back('"');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '/':
            out.push_back('/');
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'u':
            {
                if (source.size() - index < 5) {
                    return false;
                }
                unsigned char high = 0;
                unsigned char low = 0;
                if (!hex_value(source[index + 1], high) ||
                    !hex_value(source[index + 2], low) ||
                    source[index + 3] != '0' || source[index + 4] != '0') {
                    return false;
                }
                out.push_back(static_cast<char>((high << 4U) | low));
                index += 4;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

} // namespace

namespace {

[[nodiscard]] std::filesystem::path shadowdpi_data_path()
{
    constexpr DWORD kBufferSize = 4096;
    char buffer[kBufferSize]{};
    const DWORD length = GetEnvironmentVariableA("ProgramData", buffer,
                                                 kBufferSize);
    const std::string base = length > 0 && length < kBufferSize
                                 ? std::string(buffer, length)
                                 : std::string("C:\\ProgramData");
    return std::filesystem::path(base) / "ShadowDPI";
}

} // namespace

std::filesystem::path default_config_path()
{
    return shadowdpi_data_path() / "config.json";
}

std::filesystem::path default_blacklist_path()
{
    return shadowdpi_data_path() / "blacklist.txt";
}

bool save_config(const std::filesystem::path& path,
                 const std::vector<std::string>& arguments,
                 std::string& error) noexcept
{
    try {
        std::string flat;
        for (const auto& argument : arguments) {
            for (const char character : argument) {
                append_escaped(flat, static_cast<unsigned char>(character));
            }
            append_escaped(flat, 0);
        }

        const std::filesystem::path directory = path.parent_path();
        std::filesystem::create_directories(directory);

        const std::filesystem::path temporary =
            path.string() + ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                error = "cannot create config file: " + temporary.string();
                return false;
            }
            file << kPrefix << flat << kSuffix;
            file.flush();
            if (!file) {
                error = "cannot write config file: " + temporary.string();
                return false;
            }
        }
        std::filesystem::rename(temporary, path);
        return true;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::optional<std::vector<std::string>> load_config(
    const std::filesystem::path& path,
    std::string& error) noexcept
{
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            error = "cannot open config file: " + path.string();
            return std::nullopt;
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string text = contents.str();

        if (text.size() < kPrefix.size() + kSuffix.size() ||
            text.compare(0, kPrefix.size(), kPrefix) != 0 ||
            text.compare(text.size() - kSuffix.size(), kSuffix.size(),
                         kSuffix) != 0) {
            error = "config file has an unexpected format";
            return std::nullopt;
        }

        const std::string encoded =
            text.substr(kPrefix.size(),
                        text.size() - kPrefix.size() - kSuffix.size());
        std::string flat;
        if (!unescape(encoded, flat)) {
            error = "config file contains an invalid escape sequence";
            return std::nullopt;
        }

        std::vector<std::string> arguments;
        std::string current;
        for (const char character : flat) {
            if (character == '\0') {
                arguments.push_back(current);
                current.clear();
            }
            else {
                current.push_back(character);
            }
        }
        if (!current.empty()) {
            error = "config file has an unterminated argument";
            return std::nullopt;
        }
        return arguments;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return std::nullopt;
    }
}

} // namespace sdp::ipc
