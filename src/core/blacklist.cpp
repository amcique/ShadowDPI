#include "shadowdpi/blacklist.hpp"

#include <fstream>
#include <string>

#include "shadowdpi/types.hpp"
#include "shadowdpi/util.hpp"

namespace sdp {

bool Blacklist::load(const std::filesystem::path& path, std::string& error)
{
    std::ifstream file(path);
    if (!file) {
        error = "cannot open blacklist file: " + path.string();
        return false;
    }

    std::size_t added = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        const auto last = line.find_last_not_of(" \t");
        std::string_view host{line.data() + first, last - first + 1};

        if (host.empty() || host.front() == '#' || host.front() == ';') {
            continue;
        }
        // Inline comments start at the first whitespace or comment marker.
        const auto end = host.find_first_of(" \t#;");
        if (end != std::string_view::npos) {
            host = host.substr(0, end);
        }
        if (host.empty()) {
            continue;
        }
        if (host.front() == '.') {
            host.remove_prefix(1);
        }
        // A fully qualified domain may end with a dot. Matches() strips it
        // from queries, so store the same canonical form here.
        if (!host.empty() && host.back() == '.') {
            host.remove_suffix(1);
        }
        if (host.size() < 2 || host.size() > kMaxHostLength) {
            continue;
        }

        hosts_.insert(to_ascii_lower(host));
        ++added;
    }

    if (added == 0) {
        error = "blacklist file contains no valid domains: " + path.string();
        return false;
    }
    return true;
}

bool Blacklist::matches(std::string_view host) const
{
    if (host.empty() || host.size() > kMaxHostLength) {
        return false;
    }

    std::string lowered = to_ascii_lower(host);
    if (lowered.back() == '.') {
        lowered.pop_back();
    }
    if (hosts_.find(lowered) != hosts_.end()) {
        return true;
    }

    for (std::size_t position = lowered.find('.');
         position != std::string::npos;
         position = lowered.find('.', position + 1)) {
        if (hosts_.find(std::string_view{lowered}.substr(position + 1)) !=
            hosts_.end()) {
            return true;
        }
    }
    return false;
}

} // namespace sdp
