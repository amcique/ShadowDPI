#pragma once

#include <string_view>

namespace sdp {

class Logger {
public:
    explicit Logger(bool verbose) noexcept
        : verbose_(verbose)
    {
    }

    void info(std::string_view message) const;
    void warning(std::string_view message) const;
    void error(std::string_view message) const;
    void verbose(std::string_view message) const;

    [[nodiscard]] bool verbose_enabled() const noexcept
    {
        return verbose_;
    }

private:
    bool verbose_{false};
};

} // namespace sdp
