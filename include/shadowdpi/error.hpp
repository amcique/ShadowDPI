#pragma once

#include <stdexcept>

namespace sdp {

/// Thrown for any invalid command-line input so that the caller can print the
/// usage hint. Shared by the CLI parser and the config finalizer.
class CliError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace sdp
