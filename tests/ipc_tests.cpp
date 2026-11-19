#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "shadowdpi/ipc.hpp"
#include "config_store.hpp"
#include "ipc_pipe.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << __FILE__ << ':' << __LINE__                           \
                      << ": check failed: " #condition "\n";                   \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

[[nodiscard]] std::optional<std::string> current_user_sid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return std::nullopt;
    }
    DWORD size = 0;
    (void)GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<std::byte> buffer(size);
    auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
    const BOOL ok =
        GetTokenInformation(token, TokenUser, user, size, &size);
    CloseHandle(token);
    if (!ok) {
        return std::nullopt;
    }
    char* text = nullptr;
    if (!ConvertSidToStringSidA(user->User.Sid, &text)) {
        return std::nullopt;
    }
    const std::string result(text);
    LocalFree(text);
    return result;
}

void test_protocol()
{
    using namespace sdp::ipc;

    const Request request{4242, MessageType::ApplyConfig,
                          encode_argv({"--help", "-9"})};
    const auto encoded = encode_request(request);
    CHECK(encoded.size() == 16 + request.payload.size());

    const auto decoded = decode_request(encoded);
    CHECK(decoded.has_value());
    if (decoded) {
        CHECK(decoded->pid == 4242);
        CHECK(decoded->type == MessageType::ApplyConfig);
        const auto arguments = decode_argv(decoded->payload);
        CHECK(arguments.has_value());
        if (arguments) {
            CHECK(arguments->size() == 2);
            CHECK((*arguments)[0] == "--help");
        }
    }

    const Response response{true, "saved"};
    const auto encoded_response =
        encode_response(response, MessageType::Response);
    MessageType type{};
    const auto decoded_response = decode_response(encoded_response, type);
    CHECK(decoded_response.has_value());
    if (decoded_response) {
        CHECK(decoded_response->ok);
        CHECK(decoded_response->message == "saved");
        CHECK(type == MessageType::Response);
    }
}

void test_malformed()
{
    using namespace sdp::ipc;

    CHECK(!decode_request({}).has_value());
    auto bad_magic = encode_request(Request{});
    bad_magic[0] = std::byte{0};
    CHECK(!decode_request(bad_magic).has_value());

    auto bad_version = encode_request(Request{});
    bad_version[4] = std::byte{9};
    CHECK(!decode_request(bad_version).has_value());

    auto truncated = encode_request(Request{1, MessageType::GetStatus,
                                            std::vector<std::byte>(4)});
    truncated.pop_back();
    CHECK(!decode_request(truncated).has_value());

    Request oversized{};
    oversized.payload.resize(kIpcMaxPayload + 1);
    CHECK(encode_request(oversized).empty());

    const std::vector<std::byte> unterminated{
        std::byte{'a'}, std::byte{'b'}};
    CHECK(!decode_argv(unterminated).has_value());
    CHECK(decode_argv({}).has_value());

    auto bad_response =
        encode_response(Response{true, "x"}, MessageType::Response);
    bad_response[12] = std::byte{2}; // invalid ok byte
    MessageType type{};
    CHECK(!decode_response(bad_response, type).has_value());
}

void test_sddl()
{
    const auto admin_only = sdp::ipc::build_pipe_sddl(std::nullopt);
    CHECK(!admin_only.empty());
    CHECK(admin_only.find("(A;;GA;;;SY)") != std::string::npos);
    CHECK(admin_only.find("(A;;GA;;;BA)") != std::string::npos);
    CHECK(admin_only.find("(A;;") == admin_only.find("(A;;GA;;;SY)"));

    CHECK(sdp::ipc::build_pipe_sddl("not-a-sid").empty());
    const auto with_user = sdp::ipc::build_pipe_sddl("S-1-5-21-1-2-3-1001");
    CHECK(!with_user.empty());
    CHECK(with_user.find("S-1-5-21-1-2-3-1001") != std::string::npos);
}

void test_config_store()
{
    using namespace sdp::ipc;

    const auto directory = std::filesystem::temp_directory_path() /
                           ("shadowdpi-ipc-test-" +
                            std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "config.json";

    const std::vector<std::string> arguments{
        "shadowdpi.exe", "-9", "--fake-hex", "00ff"};
    std::string error;
    CHECK(save_config(path, arguments, error));
    const auto loaded = load_config(path, error);
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK(*loaded == arguments);
    }

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "{ broken }";
    }
    CHECK(!load_config(path, error).has_value());

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void test_pipe()
{
    using namespace sdp::ipc;

    const auto sid = current_user_sid();
    CHECK(sid.has_value());
    if (!sid) {
        return;
    }

    const std::string name =
        "shadowdpi-test-" + std::to_string(GetCurrentProcessId());
    IpcServer server(name, build_pipe_sddl(sid));
    server.set_handler([](const Request& request) {
        Response response;
        response.ok = true;
        response.message = "pong:" + std::to_string(
                               static_cast<unsigned int>(request.type));
        return response;
    });

    std::string error;
    CHECK(server.start(error));
    if (!error.empty()) {
        std::cerr << "server start: " << error << '\n';
    }

    auto client = IpcClient::connect(name, error);
    CHECK(client.has_value());
    if (client) {
        MessageType type{};
        const auto response = client->round_trip(
            Request{GetCurrentProcessId(), MessageType::Handshake, {}},
            type, error);
        if (!response) {
            std::cerr << "round trip: " << error << '\n';
        }
        CHECK(response.has_value());
        if (response) {
            CHECK(response->ok);
            CHECK(response->message == "pong:1");
            CHECK(type == MessageType::Response);
        }
    }

    server.stop();
}

} // namespace

int main()
{
    test_protocol();
    test_malformed();
    test_sddl();
    test_config_store();
    test_pipe();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ipc tests passed\n";
    return 0;
}
