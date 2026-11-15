#pragma once

#include <string>

#include "win_headers.hpp"

namespace sdp::win {

/// Resolved WinDivert entry points.
///
/// The bundled WinDivert import library cannot be delay-loaded by MSVC, so the
/// DLL is resolved explicitly. Loading happens after `main()` has restricted
/// the DLL search path, which keeps a planted WinDivert.dll from being picked
/// up by the Windows loader before any hardening runs.
struct WinDivertApi {
    decltype(&::WinDivertOpen) open{nullptr};
    decltype(&::WinDivertClose) close{nullptr};
    decltype(&::WinDivertRecv) recv{nullptr};
    decltype(&::WinDivertSend) send{nullptr};
    decltype(&::WinDivertShutdown) shutdown{nullptr};
    decltype(&::WinDivertHelperParsePacket) helper_parse{nullptr};
    decltype(&::WinDivertHelperCalcChecksums) helper_calc_checksums{nullptr};
};

/// Loads WinDivert.dll from the application or system directory and resolves
/// the entry points used by ShadowDPI. Safe to call more than once; returns
/// true when the API is ready.
[[nodiscard]] bool load_windivert_api(std::string& error) noexcept;

/// Returns the resolved entry points. Members are null until a successful
/// load_windivert_api() call.
[[nodiscard]] const WinDivertApi& windivert_api() noexcept;

} // namespace sdp::win
