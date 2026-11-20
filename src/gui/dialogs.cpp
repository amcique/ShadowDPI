#include "dialogs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "config_store.hpp"
#include "gui_ipc.hpp"
#include "install_script_hash.hpp"
#include "scan.hpp"
#include "shadowdpi/ipc.hpp"
#include "strings.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>

namespace sdp::gui {
namespace {

using sdp::ipc::default_blacklist_path;
using sdp::ipc::encode_argv;
using sdp::ipc::MessageType;

constexpr int kScanUrls = 1001;
constexpr int kScanUrlFile = 1002;
constexpr int kScanLevel = 1003;
constexpr int kScanProtocols = 1004;
constexpr int kScanIp = 1005;
constexpr int kScanTimeout = 1006;
constexpr int kScanDns = 1007;
constexpr int kScanProgress = 1008;
constexpr int kScanResult = 1009;
constexpr int kScanStart = 1010;
constexpr int kScanApply = 1011;
constexpr int kScanClose = 1012;

constexpr int kBlacklistText = 2001;
constexpr int kBlacklistSave = 2002;
constexpr int kBlacklistClose = 2003;

constexpr int kDnsV4 = 3001;
constexpr int kDnsV6 = 3002;
constexpr int kDnsPort = 3003;
constexpr int kDnsApply = 3004;
constexpr int kDnsClose = 3005;

constexpr int kAdvancedText = 4001;
constexpr int kAdvancedApply = 4002;
constexpr int kAdvancedClose = 4003;

HWND control(HWND parent,
             const wchar_t* class_name,
             const wchar_t* text,
             int x,
             int y,
             int width,
             int height,
             int id,
             DWORD style = WS_CHILD | WS_VISIBLE)
{
    return CreateWindowW(class_name, text, style, x, y, width, height, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                         GetModuleHandleW(nullptr), nullptr);
}

void label(HWND parent, const std::wstring& text, int x, int y, int width)
{
    control(parent, L"STATIC", text.c_str(), x, y, width, 20, 0);
}

[[nodiscard]] std::wstring window_text(HWND window, int id)
{
    const auto child = GetDlgItem(window, id);
    if (child == nullptr) {
        return {};
    }
    const int length = GetWindowTextLengthW(child);
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        GetWindowTextW(child, text.data(), length + 1);
    }
    return text;
}

[[nodiscard]] std::string narrow_text(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                          static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(), length,
                        nullptr, nullptr);
    return result;
}

[[nodiscard]] std::vector<std::string> split_arguments(const std::string& text)
{
    std::vector<std::string> result;
    std::size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() &&
               std::isspace(static_cast<unsigned char>(text[position]))) {
            ++position;
        }
        const auto start = position;
        while (position < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[position]))) {
            ++position;
        }
        if (position > start) {
            result.push_back(text.substr(start, position - start));
        }
    }
    return result;
}

[[nodiscard]] std::wstring quote_windows_argument(std::wstring_view value)
{
    std::wstring result{L'"'};
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(2 * backslashes + 1, L'\\');
            result.push_back(L'"');
        }
        else {
            result.append(backslashes, L'\\');
            result.push_back(character);
        }
        backslashes = 0;
    }
    result.append(2 * backslashes, L'\\');
    result += L'"';
    return result;
}

[[nodiscard]] std::wstring installer_script_path()
{
    wchar_t module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(GetModuleHandleW(nullptr),
                                            module_path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const auto path = std::filesystem::path(module_path);
        const auto candidate = path.parent_path() / "install.ps1";
        if (std::filesystem::exists(candidate)) {
            return candidate.wstring();
        }
    }
    return L"C:\\Program Files\\ShadowDPI\\install.ps1";
}

[[nodiscard]] std::string file_sha256(const std::filesystem::path& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    std::string result;
    if (CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        std::array<std::byte, 8192> buffer{};
        DWORD read = 0;
        bool ok = true;
        while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                        &read, nullptr) &&
               read > 0) {
            if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(
                                         buffer.data()),
                               read, 0)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            BYTE digest[32]{};
            DWORD digest_length = sizeof(digest);
            if (CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_length,
                                  0) &&
                digest_length == sizeof(digest)) {
                static constexpr char kHex[] = "0123456789abcdef";
                result.reserve(digest_length * 2);
                for (const BYTE value : digest) {
                    result.push_back(kHex[value >> 4U]);
                    result.push_back(kHex[value & 0x0FU]);
                }
            }
        }
    }
    if (hash != 0) {
        CryptDestroyHash(hash);
    }
    if (provider != 0) {
        CryptReleaseContext(provider, 0);
    }
    CloseHandle(file);
    return result;
}

[[nodiscard]] std::wstring powershell_path()
{
    wchar_t directory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const auto path = std::filesystem::path(directory) /
                      "WindowsPowerShell\\v1.0\\powershell.exe";
    return std::filesystem::exists(path) ? path.wstring() : std::wstring{};
}

[[nodiscard]] std::wstring temporary_path(const wchar_t* suffix)
{
    wchar_t directory[MAX_PATH]{};
    GetTempPathW(MAX_PATH, directory);
    const auto name = L"shadowdpi-" + std::to_wstring(GetCurrentProcessId()) +
                      L"-" + std::to_wstring(GetTickCount64()) + suffix;
    return std::wstring(directory) + name;
}

void show_message(HWND parent,
                  const std::wstring& text,
                  unsigned int flags = MB_OK | MB_ICONINFORMATION)
{
    MessageBoxW(parent, text.c_str(), widen(english().app_name).c_str(), flags);
}

struct ScanState {
    HWND window{nullptr};
    HANDLE process{nullptr};
    std::wstring json_path;
    std::wstring progress_path;
    ScanSummary summary;
    ProgressCollector collector;
};

ScanState g_scan;

void append_progress(HWND list)
{
    std::ifstream file(g_scan.progress_path);
    if (!file) {
        return;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    for (const auto& entry : g_scan.collector.consume(contents.str())) {
        const auto text = widen(entry.name + "  " + entry.verdict + "  " +
                                std::to_string(entry.targets_passed) + "/" +
                                std::to_string(entry.target_total) +
                                "  regressions=" +
                                std::to_string(entry.regressions));
        SendMessageW(list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text.c_str()));
    }
}

void finish_scan(HWND window)
{
    if (g_scan.process != nullptr) {
        CloseHandle(g_scan.process);
        g_scan.process = nullptr;
    }
    KillTimer(window, 1);
    EnableWindow(GetDlgItem(window, kScanStart), TRUE);
    EnableWindow(GetDlgItem(window, kScanClose), TRUE);

    std::ifstream file(g_scan.json_path);
    if (!file) {
        SetWindowTextW(GetDlgItem(window, kScanResult),
                       widen(english().scan_no_result).c_str());
        return;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    const auto summary = parse_scan_summary(contents.str());
    if (!summary || !summary->has_recommendation) {
        SetWindowTextW(GetDlgItem(window, kScanResult),
                       widen(english().scan_no_result).c_str());
        return;
    }
    g_scan.summary = *summary;
    const auto text = g_scan.summary.recommendation.name + "  " +
                      std::to_string(
                          g_scan.summary.recommendation.targets_passed) +
                      "/" +
                      std::to_string(
                          g_scan.summary.recommendation.target_total);
    SetWindowTextW(GetDlgItem(window, kScanResult), widen(text).c_str());
    EnableWindow(GetDlgItem(window, kScanApply), TRUE);
}

LRESULT CALLBACK scan_proc(HWND window,
                           UINT message,
                           WPARAM wparam,
                           LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        {
            const auto& strings = english();
            label(window, widen(strings.urls_label), 10, 10, 150);
            control(window, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                    170, 10, 440, 22, kScanUrls);
            label(window, widen(strings.url_file_label), 10, 40, 150);
            control(window, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                    170, 40, 440, 22, kScanUrlFile);
            label(window, widen(strings.level_label), 10, 70, 150);
            control(window, L"COMBOBOX", L"",
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                    170, 70, 150, 100, kScanLevel);
            for (const auto* level : {"quick", "standard", "full"}) {
                SendMessageW(GetDlgItem(window, kScanLevel), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(widen(level).c_str()));
            }
            SendMessageW(GetDlgItem(window, kScanLevel), CB_SETCURSEL, 1, 0);

            label(window, widen(strings.protocols_label), 330, 70, 100);
            control(window, L"COMBOBOX", L"",
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                    440, 70, 170, 100, kScanProtocols);
            for (const auto* protocol : {"auto", "tls12", "tls13", "all"}) {
                SendMessageW(GetDlgItem(window, kScanProtocols), CB_ADDSTRING,
                             0,
                             reinterpret_cast<LPARAM>(widen(protocol).c_str()));
            }
            SendMessageW(GetDlgItem(window, kScanProtocols), CB_SETCURSEL, 0,
                         0);

            label(window, widen(strings.ip_label), 10, 100, 150);
            control(window, L"COMBOBOX", L"",
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                    170, 100, 150, 100, kScanIp);
            for (const auto* version : {"4", "6", "auto"}) {
                SendMessageW(GetDlgItem(window, kScanIp), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(widen(version).c_str()));
            }
            SendMessageW(GetDlgItem(window, kScanIp), CB_SETCURSEL, 0, 0);

            label(window, widen(strings.timeout_label), 330, 100, 100);
            control(window, L"EDIT", L"5", WS_CHILD | WS_VISIBLE | WS_BORDER,
                    440, 100, 60, 22, kScanTimeout);

            label(window, widen(strings.dns_label), 10, 130, 150);
            control(window, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                    170, 130, 440, 22, kScanDns);

            label(window, widen(strings.scan_progress), 10, 160, 200);
            control(window, L"LISTBOX", L"",
                    WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL,
                    10, 185, 600, 180, kScanProgress);
            control(window, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                    10, 375, 600, 20, kScanResult);

            control(window, L"BUTTON", widen(strings.start_scan).c_str(),
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    10, 405, 120, 28, kScanStart);
            control(window, L"BUTTON",
                    widen(strings.apply_recommended).c_str(),
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                    140, 405, 220, 28, kScanApply);
            control(window, L"BUTTON", widen(strings.close).c_str(),
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    370, 405, 100, 28, kScanClose);
            return 0;
        }
    case WM_TIMER:
        append_progress(GetDlgItem(window, kScanProgress));
        if (g_scan.process != nullptr &&
            WaitForSingleObject(g_scan.process, 0) == WAIT_OBJECT_0) {
            append_progress(GetDlgItem(window, kScanProgress));
            finish_scan(window);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kScanStart:
            {
                const auto& strings = english();
                if (g_scan.process != nullptr) {
                    show_message(window, widen(strings.scan_in_progress),
                                 MB_OK | MB_ICONWARNING);
                    return 0;
                }
                const auto script = installer_script_path();
                if (!std::filesystem::exists(script) ||
                    file_sha256(script) != kInstallScriptSha256) {
                    show_message(window, widen(strings.scan_script_invalid),
                                 MB_OK | MB_ICONWARNING);
                    return 0;
                }
                const auto powershell = powershell_path();
                if (powershell.empty()) {
                    show_message(window, widen(strings.scan_elevation),
                                 MB_OK | MB_ICONWARNING);
                    return 0;
                }
                ScanOptions options;
                const auto urls = narrow_text(window_text(window, kScanUrls));
                std::string current;
                for (const auto character : urls) {
                    if (character == ',') {
                        if (!current.empty()) {
                            options.urls.push_back(current);
                            current.clear();
                        }
                    }
                    else {
                        current.push_back(character);
                    }
                }
                if (!current.empty()) {
                    options.urls.push_back(current);
                }
                options.url_file =
                    narrow_text(window_text(window, kScanUrlFile));
                const auto selection = [](HWND combo, int fallback) {
                    const auto index = static_cast<int>(
                        SendMessageW(combo, CB_GETCURSEL, 0, 0));
                    return index >= 0 ? index : fallback;
                };
                options.scan_level = selection(GetDlgItem(window, kScanLevel),
                                               1) == 0 ? "quick"
                                                       : (selection(
                                                              GetDlgItem(
                                                                  window,
                                                                  kScanLevel),
                                                              1) == 2
                                                              ? "full"
                                                              : "standard");
                const auto protocol_index =
                    selection(GetDlgItem(window, kScanProtocols), 0);
                options.protocols = protocol_index == 1 ? "tls12"
                                                        : (protocol_index == 2
                                                               ? "tls13"
                                                               : (protocol_index ==
                                                                          3
                                                                      ? "all"
                                                                      : "auto"));
                const auto ip_index = selection(GetDlgItem(window, kScanIp), 0);
                options.ip_version = ip_index == 1 ? "6"
                                                   : (ip_index == 2 ? "auto"
                                                                    : "4");
                options.timeout_sec = 5;
                try {
                    options.timeout_sec =
                        std::stoi(narrow_text(window_text(window,
                                                          kScanTimeout)));
                }
                catch (...) {
                }
                options.dns_addr = narrow_text(window_text(window, kScanDns));

                g_scan.json_path = temporary_path(L"-scan.json");
                g_scan.progress_path = temporary_path(L"-progress.jsonl");
                g_scan.collector = ProgressCollector{};
                EnableWindow(GetDlgItem(window, kScanApply), FALSE);
                SetWindowTextW(GetDlgItem(window, kScanResult), L"");
                SendMessageW(GetDlgItem(window, kScanProgress), LB_RESETCONTENT,
                             0, 0);

                const auto arguments = build_scan_arguments(
                    options, narrow_text(g_scan.json_path),
                    narrow_text(g_scan.progress_path));
                std::wstring parameters =
                    L"-NoProfile -ExecutionPolicy Bypass -File " +
                    quote_windows_argument(script);
                for (const auto& argument : arguments) {
                    parameters += L" " + quote_windows_argument(widen(argument));
                }

                SHELLEXECUTEINFOW info{};
                info.cbSize = sizeof(info);
                info.fMask = SEE_MASK_NOCLOSEPROCESS;
                info.lpVerb = L"runas";
                info.lpFile = powershell.c_str();
                info.lpParameters = parameters.c_str();
                info.nShow = SW_HIDE;
                if (!ShellExecuteExW(&info)) {
                    show_message(window, widen(strings.scan_elevation),
                                 MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (info.hProcess == nullptr) {
                    show_message(window, widen(strings.scan_elevation),
                                 MB_OK | MB_ICONWARNING);
                    return 0;
                }
                g_scan.process = info.hProcess;
                EnableWindow(GetDlgItem(window, kScanStart), FALSE);
                EnableWindow(GetDlgItem(window, kScanClose), FALSE);
                SetTimer(window, 1, 300, nullptr);
            }
            return 0;
        case kScanApply:
            {
                if (!g_scan.summary.has_recommendation) {
                    return 0;
                }
                const auto payload = encode_argv(
                    g_scan.summary.recommendation.args);
                std::string response;
                std::string error;
                const bool ok = send_request(MessageType::ApplyConfig, payload,
                                             response, error);
                show_message(window, widen(!response.empty() ? response : error),
                             MB_OK | (ok ? MB_ICONINFORMATION
                                         : MB_ICONWARNING));
            }
            return 0;
        case kScanClose:
            DestroyWindow(window);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        if (g_scan.process != nullptr) {
            show_message(window, widen(english().scan_in_progress),
                         MB_OK | MB_ICONWARNING);
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (g_scan.process != nullptr) {
            CloseHandle(g_scan.process);
            g_scan.process = nullptr;
        }
        KillTimer(window, 1);
        g_scan.window = nullptr;
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

} // namespace

void show_scan_wizard(HWND parent)
{
    (void)parent;
    if (g_scan.window != nullptr) {
        SetForegroundWindow(g_scan.window);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = scan_proc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"ShadowDPI.ScanWizard";
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        registered = RegisterClassW(&window_class) != 0;
    }
    if (!registered) {
        return;
    }
    g_scan.window = CreateWindowW(
        L"ShadowDPI.ScanWizard", widen(english().scan_title).c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, nullptr,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_scan.window != nullptr) {
        ShowWindow(g_scan.window, SW_SHOW);
    }
}

LRESULT CALLBACK blacklist_proc(HWND window,
                                UINT message,
                                WPARAM wparam,
                                LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        {
            label(window, widen(english().blacklist_hint), 10, 10, 600);
            control(window, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                        ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL |
                        WS_HSCROLL,
                    10, 35, 600, 300, kBlacklistText);
            control(window, L"BUTTON", widen(english().save).c_str(),
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    10, 350, 100, 28, kBlacklistSave);
            control(window, L"BUTTON", widen(english().close).c_str(),
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    120, 350, 100, 28, kBlacklistClose);
            std::ifstream file(default_blacklist_path());
            if (file) {
                std::ostringstream contents;
                contents << file.rdbuf();
                SetWindowTextW(GetDlgItem(window, kBlacklistText),
                               widen(contents.str()).c_str());
            }
            return 0;
        }
    case WM_COMMAND:
        if (LOWORD(wparam) == kBlacklistSave) {
            const auto text = narrow_text(window_text(window, kBlacklistText));
            std::string response;
            std::string error;
            std::vector<std::byte> payload;
            payload.reserve(text.size());
            for (const char character : text) {
                payload.push_back(static_cast<std::byte>(
                    static_cast<unsigned char>(character)));
            }
            const bool ok = send_request(MessageType::WriteBlacklist, payload,
                                         response, error);
            show_message(window, widen(!response.empty() ? response : error),
                         MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
            return 0;
        }
        if (LOWORD(wparam) == kBlacklistClose) {
            DestroyWindow(window);
            return 0;
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

void show_blacklist_editor(HWND parent)
{
    (void)parent;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = blacklist_proc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"ShadowDPI.Blacklist";
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        registered = RegisterClassW(&window_class) != 0;
    }
    if (!registered) {
        return;
    }
    HWND window = FindWindowW(L"ShadowDPI.Blacklist", nullptr);
    if (window != nullptr) {
        SetForegroundWindow(window);
        return;
    }
    window = CreateWindowW(L"ShadowDPI.Blacklist",
                           widen(english().blacklist_title).c_str(),
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           640, 430, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    if (window != nullptr) {
        ShowWindow(window, SW_SHOW);
    }
}

LRESULT CALLBACK dns_proc(HWND window,
                          UINT message,
                          WPARAM wparam,
                          LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        label(window, widen(english().dns_v4_label), 10, 10, 150);
        control(window, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                170, 10, 300, 22, kDnsV4);
        label(window, widen(english().dns_v6_label), 10, 40, 150);
        control(window, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                170, 40, 300, 22, kDnsV6);
        label(window, L"Port", 10, 70, 150);
        control(window, L"EDIT", L"53", WS_CHILD | WS_VISIBLE | WS_BORDER,
                170, 70, 80, 22, kDnsPort);
        control(window, L"BUTTON", widen(english().apply).c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 110, 100, 28, kDnsApply);
        control(window, L"BUTTON", widen(english().close).c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                120, 110, 100, 28, kDnsClose);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == kDnsApply) {
            std::vector<std::string> arguments;
            const auto v4 = narrow_text(window_text(window, kDnsV4));
            const auto v6 = narrow_text(window_text(window, kDnsV6));
            const auto port = narrow_text(window_text(window, kDnsPort));
            if (!v4.empty()) {
                arguments.push_back("--dns-addr");
                arguments.push_back(v4);
                arguments.push_back("--dns-port");
                arguments.push_back(port);
            }
            if (!v6.empty()) {
                arguments.push_back("--dnsv6-addr");
                arguments.push_back(v6);
                arguments.push_back("--dnsv6-port");
                arguments.push_back(port);
            }
            std::string response;
            std::string error;
            const bool ok = send_request(MessageType::ApplyConfig,
                                         encode_argv(arguments), response,
                                         error);
            show_message(window, widen(!response.empty() ? response : error),
                         MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
            return 0;
        }
        if (LOWORD(wparam) == kDnsClose) {
            DestroyWindow(window);
            return 0;
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

void show_dns_editor(HWND parent)
{
    (void)parent;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = dns_proc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"ShadowDPI.Dns";
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        registered = RegisterClassW(&window_class) != 0;
    }
    if (!registered) {
        return;
    }
    HWND window = FindWindowW(L"ShadowDPI.Dns", nullptr);
    if (window != nullptr) {
        SetForegroundWindow(window);
        return;
    }
    window = CreateWindowW(L"ShadowDPI.Dns", widen(english().dns_title).c_str(),
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           500, 190, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    if (window != nullptr) {
        ShowWindow(window, SW_SHOW);
    }
}

LRESULT CALLBACK advanced_proc(HWND window,
                               UINT message,
                               WPARAM wparam,
                               LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        label(window, widen(english().advanced_hint), 10, 10, 600);
        control(window, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                    ES_AUTOVSCROLL | WS_VSCROLL,
                10, 35, 600, 200, kAdvancedText);
        control(window, L"BUTTON", widen(english().apply).c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 250, 100, 28, kAdvancedApply);
        control(window, L"BUTTON", widen(english().close).c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                120, 250, 100, 28, kAdvancedClose);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == kAdvancedApply) {
            const auto text = narrow_text(window_text(window, kAdvancedText));
            std::string response;
            std::string error;
            const bool ok = send_request(MessageType::ApplyConfig,
                                         encode_argv(split_arguments(text)),
                                         response, error);
            show_message(window, widen(!response.empty() ? response : error),
                         MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
            return 0;
        }
        if (LOWORD(wparam) == kAdvancedClose) {
            DestroyWindow(window);
            return 0;
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

void show_advanced_editor(HWND parent)
{
    (void)parent;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = advanced_proc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"ShadowDPI.Advanced";
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        registered = RegisterClassW(&window_class) != 0;
    }
    if (!registered) {
        return;
    }
    HWND window = FindWindowW(L"ShadowDPI.Advanced", nullptr);
    if (window != nullptr) {
        SetForegroundWindow(window);
        return;
    }
    window = CreateWindowW(L"ShadowDPI.Advanced",
                           widen(english().advanced_title).c_str(),
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           640, 320, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    if (window != nullptr) {
        ShowWindow(window, SW_SHOW);
    }
}

} // namespace sdp::gui
