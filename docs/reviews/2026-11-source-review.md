# ShadowDPI source review, November 2026

This is the review ShadowDPI's maintainers ran against their own code before the
first public release. It is published so that readers can judge the project by
what was found and fixed rather than by claims in a README.

- **Scope:** all first-party code under `src/` and `include/`, plus
  `CMakeLists.txt`, `CMakePresets.json`, `resources/`, and `tests/`.
  `third_party/` and `build/` were excluded.
- **Result:** 20 findings: 0 Critical, 2 High, 9 Medium, 8 Low, 1 Nit.
- **Status:** every finding below is fixed in `v0.3.0`. The status column
  reflects the final state.

Each entry lists the original location as it was at review time, the impact, and
the fix that shipped.

| ID | Severity | Status | Area | Title |
|---|---|---|---|---|
| H1 | High | fixed | Runtime | Receive failure is reported as a clean exit |
| H2 | High | fixed | Filtering | Passive filter drops every inbound RST; `--ip-id` inert |
| M1 | Medium | fixed | Lifecycle | Stop callback races with filter setup and can outlive `Application` |
| M2 | Medium | fixed | Service | Unsynchronized `g_status` access |
| M3 | Medium | fixed | Service | Any `StartServiceCtrlDispatcher` failure is treated as console start |
| M4 | Medium | fixed | DNS | DNS redirect accepts unvalidated responses; spoofing/DoS |
| M5 | Medium | fixed | Trackers | Unbounded TTL/DNS maps with 30 s whole-map cleanup |
| M6 | Medium | fixed | Filtering | Blacklist does not gate the SYN/ACK window trick |
| M7 | Medium | fixed | Config | Keep-alive flags are dead; docs claim ACK waiting |
| M8 | Medium | fixed | Runtime | WinDivert send failures are ignored |
| M9 | Medium | fixed | Build/loader | DLL search mitigation cannot protect static WinDivert import |
| L1 | Low | fixed | Config | Presets 1/2 do not match their documented equivalents |
| L2 | Low | fixed | Fragmentation | Native fragmentation duplicates FIN/URG |
| L3 | Low | fixed | Filtering | Extra-port clause diverts all TCP on that port |
| L4 | Low | fixed | HTTP | Header search is case-sensitive and body-wide |
| L5 | Low | fixed | TLS | SNI extractor aborts on first malformed candidate |
| L6 | Low | fixed | Packets | `copy_packet` does not clamp `destination.length` |
| L7 | Low | fixed | Build | No `/WX`, no `/analyze`; WinDivert DLL/SYS optional |
| L8 | Low | fixed | Filtering | QUIC filter matches v1 only |
| N1 | Nit | fixed | Build/Resources | Manifest omits Win7 GUID; duplicate `enable_testing()` |

---

## High-severity findings

### H1: Receive failure terminates the loop but is reported as success

- **Severity:** High
- **Location:** `src/windows/application.cpp:323-337`, `src/windows/application.cpp:136-140`, `src/windows/service.cpp:63-72`
- **Confidence:** High

**Problem.** `Application::loop()` breaks out of the receive loop on any
`main_handle_.receive()` failure. If shutdown was not requested, it logs the
error and breaks; `run()` then unconditionally returns `0`. The service wrapper
maps `0` to `NO_ERROR`, so the Service Control Manager sees a normal stop.

```cpp
// application.cpp:323-337
while (!ShutdownCoordinator::instance().stop_requested()) {
    if (!main_handle_.receive(packet)) {
        if (!ShutdownCoordinator::instance().stop_requested()) {
            logger_.error("error receiving packet: " + last_error_message());
        }
        break;
    }
    process(packet);
}
```

```cpp
// application.cpp:136-140
loop();
shutdown_filters();
logger_.info("ShadowDPI stopped.");
return 0;
```

```cpp
// service.cpp:66-68
g_status.dwWin32ExitCode =
    result == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR;
```

**Impact.** A transient or persistent WinDivert failure silently disables the
tool. The SCM records a successful stop, so recovery/restart policies and
monitoring cannot distinguish an operator stop from a failure.

**Fix.** Return a status from `loop()`, propagate it through `run()`, and return
non-zero on unexpected receive failure. Keep the stop-requested path as success.

---

### H2: Passive filter's `#IPID#` clause is always true

- **Severity:** High
- **Location:** `src/core/filter.cpp:45-49`, `src/core/filter.cpp:88-95`, `src/core/filter.cpp:152-156`
- **Confidence:** High

**Problem.** The passive template contains `(true #IPID#)`. The expansion
produces `" or ip.Id == N"`, so the expression becomes `(true or ip.Id == N)`.
The main filter uses the intended low-ID restriction.

```cpp
// filter.cpp:45-49
constexpr std::string_view kPassiveFilterTemplate =
    "inbound and ip and tcp and !impostor and !loopback and "
    "(true #IPID#) and "
    "(tcp.SrcPort == 443 or tcp.SrcPort == 80) and tcp.Rst and "
    "#NO_LOCALNETS_V4_SRC#";
```

**Impact.** With `block_passive` enabled (presets 1-4), every inbound RST from
ports 80/443 is dropped, including legitimate server resets, and `--ip-id` is
inert for passive blocking.

**Fix.** Mirror the main filter guard:
`(ipv6 or (ip.Id >= 0x0 and ip.Id <= 0xF)) #IPID#`.

---

## Medium-severity findings

### M1: Stop callback can race with filter setup and can outlive `Application`

- **Severity:** Medium
- **Location:** `src/windows/application.cpp:121-124,261,295-298`, `src/windows/lifecycle.cpp:39-50`
- **Confidence:** Medium-High

**Problem.** The stop callback is registered before `open_filters()` and
captures `this`. `ShutdownCoordinator::request_stop()` invokes it from a
system/console/service thread. `shutdown_filters()` iterates `drop_handles_`
without synchronization while `open_filters()` can concurrently push into it.
The callback is never cleared before `Application` is destroyed.

**Impact.** Concurrent vector mutation/iteration is undefined behavior. A stop
arriving after `run()` returns invokes a callback holding a dangling `this`.

**Fix.** Register the callback only after filters open, serialize handle
mutation with a mutex, and gate callback invocation so destruction waits for any
in-flight callback.

---

### M2: Unsynchronized `g_status` access in the service

- **Severity:** Medium
- **Location:** `src/windows/service.cpp:15-16,24-31,40-54,66-72`
- **Confidence:** High

**Problem.** `g_status` is a global `SERVICE_STATUS` written by the control
handler thread and by `service_main` without a mutex or atomic. Multi-field
updates can tear and produce inconsistent SCM reports.

**Fix.** Protect every read/write with a mutex and funnel updates through a
small helper.

---

### M3: Any `StartServiceCtrlDispatcher` failure is treated as console startup

- **Severity:** Medium
- **Location:** `src/windows/service.cpp:89-95`
- **Confidence:** High

**Problem.** `run_as_service_if_requested()` returns `false` for every failure,
but only `ERROR_FAILED_SERVICE_CONTROLLER_CONNECT` means "started from a
console." Other errors are swallowed and the process falls through to
interactive mode.

**Fix.** Check `GetLastError()`; only the console error returns `false`.
Report other errors and exit non-zero.

---

### M4: DNS redirect accepts unvalidated responses (spoofing/DoS)

- **Severity:** Medium
- **Location:** `src/core/dns_tracker.cpp:41-49,64-75`, `src/windows/packet_engine.cpp:314-337`
- **Confidence:** High

**Problem.** Outgoing queries are tracked under `(ipv6, local_ip, local_port)`
only. `handle_incoming()` accepts any packet that looks like a DNS response to
that address/port, without checking the transaction ID, question, or resolver
endpoint, then erases the entry.

**Impact.** An attacker can inject a DNS-looking response to consume the entry,
delivering a rewritten packet to the application while the real response is
dropped. Matching ID/question can poison the application's resolver path.

**Fix.** Store the transaction ID and question; validate both. Validate the
incoming source against the configured resolver in the engine. Do not erase the
entry on a mismatch.

---

### M5: Unbounded TTL/DNS tracker maps; 30 s full-map cleanup

- **Severity:** Medium
- **Location:** `src/core/ttl_tracker.cpp:30-46,112-132`, `src/core/dns_tracker.cpp:41-49,93-113`
- **Confidence:** High

**Problem.** Neither tracker has a size cap. Cleanup runs at most every 30
seconds and walks the entire map on the packet thread.

**Impact.** A spoofed SYN/ACK or DNS flood can grow memory without bound for 30
seconds, and full-map cleanup causes latency spikes and packet loss.

**Fix.** Add a hard entry cap with eviction, run bounded incremental cleanup,
and shorten the cleanup interval.

---

### M6: Blacklist does not gate the SYN/ACK window-size trick

- **Severity:** Medium
- **Location:** `src/windows/packet_engine.cpp:268-300`, `README.md:34`, `docs/cli.md:105`
- **Confidence:** Medium-High

**Problem.** `process_tcp_control()` applies `change_window_size()` to inbound
SYN/ACK packets whenever fragmentation is enabled and native fragmentation is
off, with no blacklist check. Blacklist checks exist only on the SNI and Host
paths.

**Impact.** `--blacklist` is documented as scoping every trick, but the
window-size step still affects every connection on the configured ports.

**Fix.** A SYN/ACK has no Host/SNI, so the scope cannot be evaluated there.
Document that this step is global (and keep the code honest), or redesign the
step to run after the host name is known.

**Resolution.** Kept the step global, since no host name exists at SYN/ACK time.
`README.md`, `docs/cli.md`, and the engine now state this limitation explicitly.

---

### M7: Keep-alive options are dead; docs claim ACK waiting

- **Severity:** Medium
- **Location:** `src/windows/packet_engine.cpp:181-186`, `src/core/config.cpp:42-43`, `src/core/cli.cpp:371-375`, `docs/cli.md:59-60`, `include/shadowdpi/config.hpp:22-23`
- **Confidence:** High

**Problem.** `http.persistent_nowait` is never read, and no ACK-wait logic
exists. The help and docs promise behavior the code does not implement.

**Fix.** Remove the dead flag and the false claims, or implement the ACK-wait
state machine.

---

### M8: WinDivert send failures are ignored

- **Severity:** Medium
- **Location:** `src/windows/application.cpp:46-49,347-355`, `src/windows/packet_sink.hpp:14`, `src/windows/fake_sender.cpp:73`, `src/windows/packet_engine.cpp:490`
- **Confidence:** High

**Problem.** `DivertHandle::send()` returns `bool`, but every caller discards
it. `PacketSink::send()` returns `void`, so the packet engine cannot react.

**Impact.** Failed reinjection silently loses traffic; failed fake sends leave
no diagnostic or counter.

**Fix.** Return a status from `PacketSink::send()`, log rate-limited failures,
and abort remaining fragments when a fragment send fails.

---

### M9: DLL search mitigation cannot protect the static WinDivert import

- **Severity:** Medium
- **Location:** `CMakeLists.txt:152-155`, `src/windows/main.cpp:7-10`, `src/windows/application.cpp:235`
- **Confidence:** High

**Problem.** `shadowdpi` links `WinDivert.lib` as a normal import library. The
loader resolves `WinDivert.dll` before `main()` runs, so `SetDllDirectoryA("")`
cannot protect it. `LoadLibraryA("dnsapi.dll")` also uses the default search
order.

**Impact.** A malicious `WinDivert.dll` next to the executable can be loaded
before the hardening code executes. The binary normally requires administrator
privileges, so this can become a privilege-escalation vector.

**Fix.** Delay-load WinDivert, load it with explicit safe search flags before the
first WinDivert call, and load `dnsapi.dll` from `System32`.

**Resolution.** The bundled `WinDivert.lib` is a MinGW-format import library, so
MSVC `/DELAYLOAD` cannot process it. The executable therefore no longer links
`WinDivert.lib`; `windivert_api.cpp` resolves the required entry points with
`LoadLibraryExA(..., LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
LOAD_LIBRARY_SEARCH_SYSTEM32)` and `GetProcAddress`. `dnsapi.dll` is loaded from
`System32`, and the process-wide search path is restricted in `main()`.
`dumpbin /DEPENDENTS` confirms the executable has no static WinDivert import.

---

## Low-severity findings

### L1: Presets 1/2 do not match their documented command-line equivalents

- **Severity:** Low
- **Location:** `src/core/cli.cpp:568-569`, `src/core/config.cpp:32-44`
- **Confidence:** High

**Problem.** Help text says `-1` expands to `-p -r -s -f 2 -k 2 -n -e 2`, but
`apply_preset(1/2)` does not set `native_frag`. The documented expansion does.

**Fix.** Set `native_frag` in presets 1/2, or correct the help text. A test
compares the preset with its documented expansion.

---

### L2: Native fragmentation duplicates FIN/URG flags

- **Severity:** Low
- **Location:** `src/windows/packet_engine.cpp:450-491`
- **Confidence:** High

**Problem.** Both fragments copy the original TCP header. FIN, URG, and PSH are
duplicated, so the peer can see FIN on the head fragment and close early.

**Fix.** Clear FIN/URG/PSH on the head fragment and leave them on the tail.

---

### L3: Extra-port filter clause diverts all TCP on that port

- **Severity:** Low
- **Location:** `src/core/filter.cpp:118-125`
- **Confidence:** High

**Problem.** The extra-port clause matches pure ACKs, SYNs, RSTs, and both
directions, unlike the narrow main filter.

**Fix.** Require `tcp.Ack` and payload, and restrict each direction to the
appropriate port role; keep inbound SYN/ACK for the window trick.

---

### L4: HTTP header search is case-sensitive and body-wide

- **Severity:** Low
- **Location:** `src/core/http.cpp:30-49,67-92`
- **Confidence:** High

**Problem.** `find_bytes()` is case-sensitive, so lowercase `host:` is missed.
The search spans the whole payload, so body text can be mistaken for a header.

**Fix.** Search only the header block and compare header names
case-insensitively.

---

### L5: SNI extractor aborts on the first malformed candidate

- **Severity:** Low
- **Location:** `src/core/tls.cpp:52-60`
- **Confidence:** High

**Problem.** The heuristic scanner returns `std::nullopt` immediately when a
candidate length or offset is invalid, instead of continuing to scan.

**Impact.** A decoy pattern before the real SNI suppresses extraction.

**Fix.** `continue` on invalid candidates and bound the scan to the handshake.

---

### L6: `copy_packet` does not clamp the copied length

- **Severity:** Low
- **Location:** `src/windows/packet.cpp:173-181`
- **Confidence:** High that the bug exists; Low current exploitability.

**Problem.** `destination.length = source.length` is assigned before clamping
the copied byte count, so a future caller with an oversized source would leave a
descriptor larger than its storage.

**Fix.** Clamp the logical length to the destination buffer size.

---

### L7: Build lacks warnings-as-errors/static analysis; WinDivert runtime files are optional

- **Severity:** Low
- **Location:** `CMakeLists.txt:64-66,84-92,159-161,166-190`
- **Confidence:** High

**Problem.** Targets use `/W4` but no `/WX` or `/analyze`. `WinDivert.dll` and
`WinDivert64.sys` are copied only if found, so a binary can build but fail at
startup.

**Fix.** Add an opt-in warnings-as-errors mode and a strict runtime-file mode,
plus hardening flags.

---

### L8: QUIC filter only matches QUIC v1

- **Severity:** Low
- **Location:** `src/core/filter.cpp:158-163`
- **Confidence:** High

**Problem.** The filter matches only version `0x00000001`; QUIC v2 uses
`0x6b3343cf`.

**Fix.** Match both versions.

---

## Nit

### N1: Manifest omits the Windows 7 `supportedOS` GUID; `enable_testing()` is duplicated

- **Severity:** Nit
- **Location:** `resources/shadowdpi.exe.manifest:12-17`, `CMakeLists.txt:196,246`
- **Confidence:** High

**Problem.** The compatibility section omits Windows 7 despite `README.md:178`
mentioning it. `enable_testing()` is called twice.

**Fix.** Add the Win7 GUID and call `enable_testing()` once.

---

## Top priorities

1. **H1**: propagate runtime receive failure so the service cannot report
   success while protection is off.
2. **H2**: restore the passive IP-ID restriction and test the generated filter.
3. **M4**: validate DNS transaction ID, question, and resolver endpoint before
   consuming tracker entries.
4. **M1**: fix the shutdown/open-filter race alongside H1.

## Test coverage added with the fixes

- DNS spoofing: wrong source, wrong transaction ID, wrong question, duplicate
  responses, and tracker cap.
- TTL tracker cap under a SYN/ACK flood.
- Passive-filter IP-ID guard and QUIC v1/v2 matching.
- Extra-port clause requiring acknowledgements and payload.
- Lowercase HTTP headers and body decoys.
- Decoy-before-real SNI.
- Native fragmentation FIN/URG/PSH flags.
- Fragment, fake-request, and reinjection send failures.
- Oversized `copy_packet` input.
- Preset 1/2 equivalence with their documented expansions.

## Remaining coverage gaps

- Service lifecycle and SCM status transitions (needs a service harness).
- Concurrent stop during filter startup and callback lifetime (needs fault
  injection around WinDivert).
- Package-time WinDivert runtime files (covered by
  `SHADOWDPI_REQUIRE_WINDIVERT_RUNTIME` at configure time).

## Positive observations

- Clean split between `shadowdpi_core` and the Windows layer.
- RAII for WinDivert handles.
- Bounds-checked packet parsing with parity tests against WinDivert and a
  randomized corpus.
- Correct checksum fixups and scratch-buffer reuse.
- Thorough CLI/config validation.
- Boundary-aware, case-insensitive blacklist matching.
- Useful core test coverage, including DNS, passive DPI, and fragmentation.
- Ready-file support, verbose logging, service/console dual mode.
- `/DYNAMICBASE`, `/NXCOMPAT`, and an administrator manifest.
