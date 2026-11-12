#pragma once

namespace sdp::win {

/// Attempts to connect to the Windows service control manager.
/// Returns true when the process was started as a service and the service
/// stopped normally; false when the program should continue as a console app.
bool run_as_service_if_requested(int argc, char* argv[]);

} // namespace sdp::win
