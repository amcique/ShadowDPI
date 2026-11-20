#include "gui_ipc.hpp"

#include "ipc_pipe.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace sdp::gui {

std::wstring widen(std::string_view text)
{
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(), length);
    return result;
}

bool send_request(sdp::ipc::MessageType type,
                  const std::vector<std::byte>& payload,
                  std::string& response,
                  std::string& error)
{
    auto client = sdp::ipc::IpcClient::connect("shadowdpi", error);
    if (!client) {
        return false;
    }
    sdp::ipc::MessageType response_type{};
    const auto reply = client->round_trip(
        sdp::ipc::Request{GetCurrentProcessId(), type, payload},
        response_type, error);
    if (!reply) {
        return false;
    }
    response = reply->message;
    return reply->ok;
}

} // namespace sdp::gui
