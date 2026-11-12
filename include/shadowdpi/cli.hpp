#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "shadowdpi/config.hpp"

namespace sdp {

class CliError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Parses argv into a configuration. Throws CliError on invalid input.
[[nodiscard]] Config parse_cli(int argc, char* argv[]);

[[nodiscard]] std::string help_text();
[[nodiscard]] std::string version_text();

} // namespace sdp
