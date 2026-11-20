#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "shadowdpi/ipc.hpp"

namespace sdp::gui {

[[nodiscard]] std::wstring widen(std::string_view text);

[[nodiscard]] bool send_request(sdp::ipc::MessageType type,
                                const std::vector<std::byte>& payload,
                                std::string& response,
                                std::string& error);

} // namespace sdp::gui
