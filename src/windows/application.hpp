#pragma once

namespace sdp::win {

/// Parses the command line, configures WinDivert and runs the packet loop.
/// Returns a process exit code.
int run_application(int argc, char* argv[]);

} // namespace sdp::win
