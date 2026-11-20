#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "shadowdpi/ipc.hpp"
#include "ipc_pipe.hpp"
#include "presets.hpp"
#include "strings.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kStatusTimer = 1;
constexpr int kMenuDashboard = 1001;
constexpr int kMenuPresets = 2000;
constexpr int kMenuStop = 3001;
constexpr int kMenuExit = 3002;
constexpr int kDashboardRefresh = 4001;
constexpr int kDashboardClose = 4002;

HWND g_window = nullptr;
NOTIFYICONDATAW g_tray{};
HWND g_dashboard = nullptr;
HWND g_journal = nullptr;
HWND g_state = nullptr;
HWND g_preset = nullptr;
HWND g_pid = nullptr;
std::vector<sdp::gui::Preset> g_presets;

[[nodiscard]] std::wstring widen(std::string_view text)
{
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(),
                        length);
    return result;
}

[[nodiscard]] std::string narrow(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                          static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(),
                        length, nullptr, nullptr);
    return result;
}

[[nodiscard]] bool send_request(sdp::ipc::MessageType type,
                                const std::vector<std::byte>& payload,
                                std::string& response_text,
                                std::string& error)
{
    auto client = sdp::ipc::IpcClient::connect("shadowdpi", error);
    if (!client) {
        return false;
    }
    sdp::ipc::MessageType response_type{};
    const auto response = client->round_trip(
        sdp::ipc::Request{GetCurrentProcessId(), type, payload},
        response_type, error);
    if (!response) {
        return false;
    }
    response_text = response->message;
    return response->ok;
}

void update_tray(std::string_view text)
{
    const auto tip = widen(text).substr(0, 255);
    g_tray.szTip[0] = L'\0';
    wcsncpy_s(g_tray.szTip, ARRAYSIZE(g_tray.szTip), tip.c_str(),
              ARRAYSIZE(g_tray.szTip) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

void refresh_status()
{
    std::string response;
    std::string error;
    const auto& strings = sdp::gui::english();
    if (!send_request(sdp::ipc::MessageType::GetStatus, {}, response, error)) {
        update_tray(strings.status_unavailable);
        return;
    }

    try {
        const auto status = nlohmann::json::parse(response);
        const auto state = status.value("state", 0U) == 4;
        const auto preset = status.value("preset", std::string{});
        std::string text{state ? strings.status_running
                               : strings.status_stopped};
        if (!preset.empty()) {
            text += " (" + preset + ")";
        }
        update_tray(text);

        if (g_dashboard != nullptr) {
            SetWindowTextW(g_state, widen(state ? strings.status_running
                                                : strings.status_stopped)
                                        .c_str());
            SetWindowTextW(g_preset, widen(preset).c_str());
            SetWindowTextW(g_pid,
                           std::to_wstring(status.value("pid", 0U)).c_str());
        }
    }
    catch (...) {
        update_tray(strings.parse_error);
    }
}

void refresh_journal()
{
    if (g_journal == nullptr) {
        return;
    }
    std::string response;
    std::string error;
    if (!send_request(sdp::ipc::MessageType::GetLog, {}, response, error)) {
        return;
    }
    try {
        const auto log = nlohmann::json::parse(response);
        SendMessageW(g_journal, LB_RESETCONTENT, 0, 0);
        for (const auto& line : log.value("lines", nlohmann::json::array())) {
            const auto text = widen(line.get<std::string>());
            SendMessageW(g_journal, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(text.c_str()));
        }
    }
    catch (...) {
    }
}

void apply_preset(std::size_t index)
{
    if (index >= g_presets.size()) {
        return;
    }
    std::string response;
    std::string error;
    const auto payload = sdp::ipc::encode_argv(g_presets[index].arguments);
    const bool ok = send_request(sdp::ipc::MessageType::ApplyConfig, payload,
                                 response, error);
    const auto& strings = sdp::gui::english();
    MessageBoxW(g_window, widen(ok ? strings.preset_applied
                                   : (response.empty() ? error : response))
                              .c_str(),
                widen(strings.app_name).c_str(), MB_OK | MB_ICONINFORMATION);
}

void request_stop()
{
    std::string response;
    std::string error;
    const bool ok =
        send_request(sdp::ipc::MessageType::Stop, {}, response, error);
    const auto& strings = sdp::gui::english();
    MessageBoxW(g_window,
                widen(ok ? strings.stop_requested : strings.stop_failed)
                    .c_str(),
                widen(strings.app_name).c_str(), MB_OK | MB_ICONINFORMATION);
}

void show_tray_menu()
{
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(g_window);

    const auto& strings = sdp::gui::english();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuDashboard,
                widen(strings.open_dashboard).c_str());

    HMENU presets = CreatePopupMenu();
    for (std::size_t index = 0; index < g_presets.size(); ++index) {
        const auto label = std::string(g_presets[index].name) + "  " +
                           std::string(g_presets[index].description);
        AppendMenuW(presets, MF_STRING,
                    kMenuPresets + static_cast<UINT>(index),
                    widen(label).c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(presets),
                widen(strings.presets_menu).c_str());
    AppendMenuW(menu, MF_STRING, kMenuStop,
                widen(strings.emergency_stop).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, widen(strings.exit).c_str());

    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, g_window,
                   nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK dashboard_proc(HWND window,
                                UINT message,
                                WPARAM wparam,
                                LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        {
            const auto& strings = sdp::gui::english();
            g_state = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10,
                                    10, 360, 20, window, nullptr, nullptr,
                                    nullptr);
            g_preset = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10,
                                     35, 360, 20, window, nullptr, nullptr,
                                     nullptr);
            g_pid = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 60,
                                  360, 20, window, nullptr, nullptr, nullptr);
            CreateWindowW(L"STATIC", widen(strings.journal_label).c_str(),
                          WS_CHILD | WS_VISIBLE, 10, 90, 360, 20, window,
                          nullptr, nullptr, nullptr);
            g_journal = CreateWindowW(
                L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                    LBS_NOTIFY,
                10, 115, 440, 240, window, nullptr, nullptr, nullptr);
            CreateWindowW(L"BUTTON", widen(strings.refresh).c_str(),
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 370, 100,
                          28, window,
                          reinterpret_cast<HMENU>(
                              static_cast<INT_PTR>(kDashboardRefresh)),
                          nullptr,
                          nullptr);
            CreateWindowW(L"BUTTON", widen(strings.close).c_str(),
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 120, 370, 100,
                          28, window,
                          reinterpret_cast<HMENU>(
                              static_cast<INT_PTR>(kDashboardClose)),
                          nullptr,
                          nullptr);
            refresh_status();
            refresh_journal();
            return 0;
        }
    case WM_COMMAND:
        if (LOWORD(wparam) == kDashboardRefresh) {
            refresh_status();
            refresh_journal();
        }
        else if (LOWORD(wparam) == kDashboardClose) {
            DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY:
        g_dashboard = nullptr;
        g_journal = nullptr;
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

void show_dashboard()
{
    if (g_dashboard != nullptr) {
        SetForegroundWindow(g_dashboard);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = dashboard_proc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"ShadowDPI.Dashboard";
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        registered = RegisterClassW(&window_class) != 0;
    }
    if (!registered) {
        return;
    }

    const auto& strings = sdp::gui::english();
    g_dashboard = CreateWindowW(
        L"ShadowDPI.Dashboard", widen(strings.dashboard_title).c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 480, 450, nullptr,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_dashboard != nullptr) {
        ShowWindow(g_dashboard, SW_SHOW);
        UpdateWindow(g_dashboard);
    }
}

LRESULT CALLBACK tray_proc(HWND window,
                           UINT message,
                           WPARAM wparam,
                           LPARAM lparam)
{
    switch (message) {
    case kTrayMessage:
        if (LOWORD(lparam) == WM_RBUTTONUP) {
            show_tray_menu();
        }
        else if (LOWORD(lparam) == WM_LBUTTONDBLCLK) {
            show_dashboard();
        }
        return 0;
    case WM_TIMER:
        if (wparam == kStatusTimer) {
            refresh_status();
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kMenuDashboard:
            show_dashboard();
            break;
        case kMenuStop:
            request_stop();
            break;
        case kMenuExit:
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            KillTimer(window, kStatusTimer);
            PostQuitMessage(0);
            break;
        default:
            if (LOWORD(wparam) >= kMenuPresets) {
                apply_preset(LOWORD(wparam) - kMenuPresets);
            }
            break;
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,
                    HINSTANCE,
                    PWSTR,
                    int)
{
    const auto& strings = sdp::gui::english();
    g_presets = sdp::gui::presets();

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = tray_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"ShadowDPI.Tray";
    if (RegisterClassW(&window_class) == 0) {
        return 1;
    }

    g_window = CreateWindowW(L"ShadowDPI.Tray", L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, instance, nullptr);
    if (g_window == nullptr) {
        return 1;
    }

    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = kTrayMessage;
    // IDI_APPLICATION is declared as LPSTR in some SDK headers; use its
    // numeric resource id directly to keep the W call warning-free.
    g_tray.hIcon = LoadIconW(
        nullptr, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(32513)));
    wcsncpy_s(g_tray.szTip, ARRAYSIZE(g_tray.szTip),
              widen(strings.status_unavailable).c_str(),
              ARRAYSIZE(g_tray.szTip) - 1);
    if (Shell_NotifyIconW(NIM_ADD, &g_tray) == FALSE) {
        return 1;
    }

    SetTimer(g_window, kStatusTimer, 2000, nullptr);
    refresh_status();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
