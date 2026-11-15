#pragma once

#include <optional>

namespace sdp::win {

/// Attempts to connect to the Windows service control manager.
/// Returns std::nullopt when the process was started from a console and should
/// continue as a console app. Otherwise returns the process exit code.
std::optional<int> run_as_service_if_requested(int argc, char* argv[]);

} // namespace sdp::win
