#include "shadowdpi/log.hpp"

#include <iostream>
#include <string>

namespace sdp {

void Logger::info(std::string_view message) const
{
    std::cout << message;
    if (message.empty() || message.back() != '\n') {
        std::cout << '\n';
    }
}

void Logger::warning(std::string_view message) const
{
    std::cerr << "warning: " << message;
    if (message.empty() || message.back() != '\n') {
        std::cerr << '\n';
    }
}

void Logger::error(std::string_view message) const
{
    std::cerr << "error: " << message;
    if (message.empty() || message.back() != '\n') {
        std::cerr << '\n';
    }
}

void Logger::verbose(std::string_view message) const
{
    if (!verbose_) {
        return;
    }
    std::cout << "debug: " << message;
    if (message.empty() || message.back() != '\n') {
        std::cout << '\n';
    }
}

} // namespace sdp
