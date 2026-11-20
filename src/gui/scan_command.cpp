#include "scan_command.hpp"

namespace sdp::gui {
namespace {

[[nodiscard]] std::wstring quote(std::wstring_view value)
{
    std::wstring result{L'\''};
    for (const wchar_t character : value) {
        if (character == L'\'') {
            result += L"''";
        }
        else {
            result.push_back(character);
        }
    }
    result += L'\'';
    return result;
}

} // namespace

std::wstring build_elevated_command(std::wstring_view expected_hash,
                                    std::wstring_view script_path,
                                    const std::vector<std::wstring>& arguments)
{
    // The elevated process reads and hashes the bytes itself, then executes
    // those same bytes from a scriptblock. A same-user racing process cannot
    // substitute the script between the check and execution.
    std::wstring command =
        L"$e=" + quote(expected_hash) + L";$p=" + quote(script_path) +
        L";$b=[IO.File]::ReadAllBytes($p);"
        L"$m=New-Object IO.MemoryStream(,$b);"
        L"$h=(Get-FileHash -InputStream $m -Algorithm SHA256).Hash.ToLowerInvariant();"
        L"if($h -ne $e){exit 3};"
        L"$t=[Text.Encoding]::UTF8.GetString($b);"
        L"$s=[ScriptBlock]::Create($t);& $s";

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (!argument.empty() && argument[0] == L'-') {
            command += L" " + argument;
            if (argument == L"-Urls" && index + 1 < arguments.size()) {
                const auto& value = arguments[++index];
                std::wstring array;
                std::size_t start = 0;
                while (start < value.size()) {
                    const auto comma = value.find(L',', start);
                    const auto length = comma == std::wstring::npos
                                            ? value.size() - start
                                            : comma - start;
                    if (!array.empty()) {
                        array += L",";
                    }
                    array += quote(value.substr(start, length));
                    if (comma == std::wstring::npos) {
                        break;
                    }
                    start = comma + 1;
                }
                command += L" " + array;
            }
            continue;
        }
        command += L" " + quote(argument);
    }
    return command;
}

} // namespace sdp::gui
