#pragma once

#include <string>
#include <string_view>

#include "shadowdpi/config.hpp"
#include "shadowdpi/error.hpp"

namespace sdp {

/// Parses argv into a configuration. Throws CliError on invalid input.
[[nodiscard]] Config parse_cli(int argc, char* argv[]);

[[nodiscard]] std::string help_text();
[[nodiscard]] std::string version_text();

} // namespace sdp
