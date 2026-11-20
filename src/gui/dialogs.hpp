#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace sdp::gui {

void show_scan_wizard(HWND parent);
void show_blacklist_editor(HWND parent);
void show_dns_editor(HWND parent);
void show_advanced_editor(HWND parent);

} // namespace sdp::gui
