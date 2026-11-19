#pragma once

#include <functional>
#include <optional>

namespace sdp::win {

/// Attempts to connect to the Windows service control manager.
/// Returns std::nullopt when the process was started from a console and should
/// continue as a console app. Otherwise returns the process exit code.
std::optional<int> run_as_service_if_requested(int argc, char* argv[]);

/// Registers the transition from START_PENDING to RUNNING. Console runs have
/// no callback, so signalling is a no-op there.
void set_service_ready_callback(std::function<void()> callback);

void signal_service_ready() noexcept;

} // namespace sdp::win
