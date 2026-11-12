#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>

namespace sdp {

/// Case-insensitive set of domain names with subdomain suffix matching.
class Blacklist {
public:
    bool load(const std::filesystem::path& path, std::string& error);

    [[nodiscard]] bool matches(std::string_view host) const;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return hosts_.size();
    }

private:
    struct TransparentHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view value) const noexcept
        {
            return std::hash<std::string_view>{}(value);
        }

        std::size_t operator()(const std::string& value) const noexcept
        {
            return (*this)(std::string_view{value});
        }
    };

    struct TransparentEqual {
        using is_transparent = void;

        bool operator()(std::string_view lhs, std::string_view rhs) const noexcept
        {
            return lhs == rhs;
        }
    };

    std::unordered_set<std::string, TransparentHash, TransparentEqual> hosts_;
};

} // namespace sdp
