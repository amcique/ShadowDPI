#include "presets.hpp"

#include <array>

namespace sdp::gui {
namespace {

std::vector<std::string> split(std::string_view arguments)
{
    std::vector<std::string> result;
    std::size_t position = 0;
    while (position < arguments.size()) {
        const auto space = arguments.find(' ', position);
        const auto length = space == std::string_view::npos
                                ? arguments.size() - position
                                : space - position;
        result.emplace_back(arguments.substr(position, length));
        if (space == std::string_view::npos) {
            break;
        }
        position = space + 1;
    }
    return result;
}

} // namespace

std::vector<Preset> presets()
{
    return {
        {"-1", "Passive blocking with HTTP Host tricks and TLS split 2",
         split("-p -r -s -f 2 -k 2 -n -e 2")},
        {"-2", "Same as -1 with TLS split 40",
         split("-p -r -s -f 2 -k 2 -n -e 40")},
        {"-3", "Passive blocking and TLS split 40",
         split("-p -r -s -e 40")},
        {"-4", "Passive blocking and Host tricks only",
         split("-p -r -s")},
        {"-5", "Splitting, reverse order and automatic fake TTL",
         split("-f 2 -e 2 --auto-ttl --reverse-frag --max-payload")},
        {"-6", "Splitting and wrong-sequence fake packets",
         split("-f 2 -e 2 --wrong-seq --reverse-frag --max-payload")},
        {"-7", "Splitting and bad-checksum fake packets",
         split("-f 2 -e 2 --wrong-chksum --reverse-frag --max-payload")},
        {"-8", "Splitting with wrong sequence and bad checksum",
         split("-f 2 -e 2 --wrong-seq --wrong-chksum --reverse-frag --max-payload")},
        {"-9", "Preset -8 plus QUIC blocking (default)", split("-9")},
    };
}

} // namespace sdp::gui
