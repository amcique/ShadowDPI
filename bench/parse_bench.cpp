// Micro-benchmark for the packet parser.
//
// Compares ShadowDPI's hand-rolled fast path against
// WinDivertHelperParsePacket on the packet shapes that dominate real traffic.
// Run without arguments to print timings; pass --check to fail when the fast
// path stops being meaningfully faster (CTest runs this in Release builds).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>

#include "packet.hpp"
#include "windivert_api.hpp"

namespace {

constexpr UINT kIterations = 2'000'000;
constexpr UINT kWarmupIterations = 20'000;

// The fast path must beat the generic helper by at least this factor. The
// observed ratio is above 5x, so this only catches real regressions and stays
// robust on slow or noisy machines.
constexpr double kMinimumSpeedup = 1.5;

struct PacketTemplate {
    std::array<std::byte, sdp::kMaxPacketSize> data{};
    UINT length{0};
    const char* name{};
};

struct SinkResult {
    void* payload{nullptr};
    UINT payload_length{0};
};

struct Timing {
    double helper_ns{0.0};
    double fast_ns{0.0};

    [[nodiscard]] double speedup() const noexcept
    {
        return fast_ns > 0.0 ? helper_ns / fast_ns : 0.0;
    }
};

template <typename Function>
[[nodiscard]] double measure(UINT iterations, Function&& function)
{
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t sink = 0;
    for (UINT index = 0; index < iterations; ++index) {
        const auto result = function();
        sink += reinterpret_cast<std::uintptr_t>(result.payload);
        sink += result.payload_length;
    }
    const auto end = std::chrono::steady_clock::now();

    // Keep the loop observable without paying for a store every iteration.
    volatile std::uint64_t keep = sink;
    (void)keep;

    const auto nanoseconds =
        std::chrono::duration<double, std::nano>(end - start).count();
    return nanoseconds / static_cast<double>(iterations);
}

[[nodiscard]] PacketTemplate make_ipv4_tcp()
{
    PacketTemplate packet;
    packet.name = "IPv4 TCP, 20-byte headers";
    constexpr UINT kPayloadSize = 800;
    packet.length = 20 + 20 + kPayloadSize;

    auto* ip = reinterpret_cast<PWINDIVERT_IPHDR>(packet.data.data());
    ip->Version = 4;
    ip->HdrLength = 5;
    ip->Length = htons(static_cast<UINT16>(packet.length));
    ip->TTL = 64;
    ip->Protocol = IPPROTO_TCP;

    auto* tcp =
        reinterpret_cast<PWINDIVERT_TCPHDR>(packet.data.data() + 20);
    tcp->HdrLength = 5;
    tcp->SrcPort = htons(51000);
    tcp->DstPort = htons(443);
    tcp->Ack = 1;

    std::memset(packet.data.data() + 40, 0x16, kPayloadSize);
    return packet;
}

[[nodiscard]] PacketTemplate make_ipv6_tcp()
{
    PacketTemplate packet;
    packet.name = "IPv6 TCP, no extension headers";
    constexpr UINT kPayloadSize = 800;
    packet.length = 40 + 20 + kPayloadSize;

    auto* ip = reinterpret_cast<PWINDIVERT_IPV6HDR>(packet.data.data());
    ip->Version = 6;
    ip->Length = htons(static_cast<UINT16>(20 + kPayloadSize));
    ip->NextHdr = IPPROTO_TCP;
    ip->HopLimit = 64;

    auto* tcp =
        reinterpret_cast<PWINDIVERT_TCPHDR>(packet.data.data() + 40);
    tcp->HdrLength = 5;
    tcp->SrcPort = htons(51000);
    tcp->DstPort = htons(443);
    tcp->Ack = 1;

    std::memset(packet.data.data() + 60, 0x16, kPayloadSize);
    return packet;
}

[[nodiscard]] Timing benchmark(PacketTemplate& packet)
{
    auto helper_parse = [&]() -> SinkResult {
        PWINDIVERT_IPHDR ipv4 = nullptr;
        PWINDIVERT_IPV6HDR ipv6 = nullptr;
        PWINDIVERT_TCPHDR tcp = nullptr;
        PWINDIVERT_UDPHDR udp = nullptr;
        PVOID payload = nullptr;
        UINT payload_length = 0;
        WinDivertHelperParsePacket(packet.data.data(), packet.length, &ipv4,
                                   &ipv6, nullptr, nullptr, nullptr, &tcp, &udp,
                                   &payload, &payload_length, nullptr, nullptr);
        return {payload, payload_length};
    };

    auto fast_parse = [&]() -> SinkResult {
        const auto parsed =
            sdp::win::parse_packet(packet.data.data(), packet.length);
        return {parsed.payload, parsed.payload_length};
    };

    for (UINT index = 0; index < kWarmupIterations; ++index) {
        (void)helper_parse();
        (void)fast_parse();
    }

    Timing timing;
    timing.helper_ns = measure(kIterations, helper_parse);
    timing.fast_ns = measure(kIterations, fast_parse);
    return timing;
}

} // namespace

int main(int argc, char** argv)
{
    std::string load_error;
    if (!sdp::win::load_windivert_api(load_error)) {
        std::cerr << load_error << '\n';
        return 1;
    }

    const bool check_requested =
        argc > 1 && std::string_view(argv[1]) == "--check";

    // Timings only mean something in an optimized build; unoptimized code
    // still prints numbers but must not fail the test.
#ifdef NDEBUG
    constexpr bool kOptimizedBuild = true;
#else
    constexpr bool kOptimizedBuild = false;
#endif
    if (check_requested && !kOptimizedBuild) {
        std::cout << "note: timing checks are skipped in unoptimized builds\n";
        return 0;
    }
    const bool check = check_requested;

    std::array<PacketTemplate, 2> packets{make_ipv4_tcp(), make_ipv6_tcp()};

    bool failed = false;
    for (auto& packet : packets) {
        const Timing timing = benchmark(packet);
        std::cout << packet.name << '\n'
                  << "  WinDivertHelperParsePacket: " << timing.helper_ns
                  << " ns/packet\n"
                  << "  ShadowDPI parse_packet:     " << timing.fast_ns
                  << " ns/packet\n"
                  << "  speedup:                    " << timing.speedup() << "x\n";

        if (check && timing.speedup() < kMinimumSpeedup) {
            std::cerr << "parser is less than " << kMinimumSpeedup
                      << "x faster than WinDivertHelperParsePacket\n";
            failed = true;
        }
    }

    return failed ? 1 : 0;
}
