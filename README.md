<p align="center">
  <img src="resources/logo.png" alt="ShadowDPI logo" width="150">
</p>

<h1 align="center">ShadowDPI</h1>

<p align="center">
  <strong>Deep Packet Inspection circumvention for Windows x64.</strong><br>
  A modern C++20 implementation that does more with less CPU.
</p>

<p align="center">
  <a href="https://github.com/amcique/ShadowDPI/releases"><img src="https://img.shields.io/badge/platform-Windows%20x64-0078D6?logo=windows11&logoColor=white" alt="Windows x64"></a>
  <a href="#build-from-source-msvc-x64"><img src="https://img.shields.io/badge/compiler-MSVC%20x64-5C2D91" alt="MSVC x64"></a>
  <a href="#build-from-source-msvc-x64"><img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white" alt="C++20"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue" alt="GPLv3"></a>
</p>

ShadowDPI sits between Windows and the network and rewrites the opening packets
of a connection so that DPI equipment cannot classify it. It runs in user space
on top of WinDivert, needs no proxy configuration, installs no root
certificate, and never terminates TLS. Applications keep talking to the real
destination.

## What it does

- **Blocks passive DPI.** Injected redirects and TCP resets are dropped before
  they reach the application.
- **Defeats active DPI.** HTTP requests and TLS ClientHellos are split,
  reordered, and rewritten; optional decoy packets carry a short TTL, a bad
  checksum, or an old sequence number.
- **Handles QUIC.** `-9` drops QUIC/HTTP3 Initial packets so browsers fall back
  to TCP, where the other techniques apply.
- **Scopes the work.** A blacklist can limit every trick to selected domains.
- **Redirects poisoned DNS.** IPv4 and IPv6 resolver addresses can be replaced
  on the fly.

## Performance

Compared with GoodbyeDPI v0.2.3rc3 on the same host, with the same workloads,
the same byte counts, and interleaved rounds, ShadowDPI used **67.8× less CPU
per gigabyte** while wall-clock transfer time stayed unchanged.

![CPU cost per GiB](docs/performance/cpu-per-gib.svg)

The apparent anomaly is the point: the two variants move data at the same
speed, but ShadowDPI burns far fewer cycles doing it. That leaves headroom for
routers, VPNs, and other software sharing the machine.

![Wall-clock parity](docs/performance/wall-clock-parity.svg)

The A/A control runs the original binary in both slots. Its median ratio was
1.193 with a range of 0.766 to 1.547, so differences of tens of percent are
normal on this host. The A/B medians for HTTP, HTTPS, and keep-alive all fall
inside the A/A range; the paired CPU ratios (0.0023, 0.0011, 0.0911) do not.

### CPU cost for all twelve workloads

![CPU cycles by workload](docs/performance/cpu-cycles.svg)

Bulk HTTPS transfers show 500-810× less CPU. The remaining workloads show
2-75×; the 67.8× byte-weighted figure above is the conservative aggregate.

Method, environment, the full matrix, latency distributions, and caveats are
in [docs/performance.md](docs/performance.md).

## Getting started

1. Download the [latest release](https://github.com/amcique/ShadowDPI/releases).
2. Unpack it anywhere. The archive already contains `WinDivert.dll` and
   `WinDivert64.sys`.
3. Run `shadowdpi.exe` from an elevated prompt, or double-click it and accept
   the UAC prompt.

With no arguments ShadowDPI applies `-9`, which is the right first try for most
providers. When updating, replace `shadowdpi.exe`, `WinDivert.dll`, and
`WinDivert64.sys` together so their versions stay matched.

If the provider poisons DNS answers, either enable secure DNS in the browser or
redirect DNS at the packet layer:

```powershell
.\shadowdpi.exe -9 --dns-addr 77.88.8.8 --dns-port 1253
```

## Tuning

Presets are additive groups of options; later arguments override them.

| Preset | What it adds |
| --- | --- |
| `-1` … `-4` | Passive blocking plus HTTP Host tricks; TLS split is 2, 40, or off |
| `-5` | HTTP/TLS splitting, reverse order, and automatic fake TTL |
| `-6` | Splitting plus fake packets with a wrong TCP sequence number |
| `-7` | Splitting plus fake packets with a bad checksum |
| `-8` | Both wrong sequence and bad checksum |
| `-9` | `-8` plus QUIC blocking (the default) |

Useful variations:

```powershell
.\shadowdpi.exe -5                                  # if -9 is too aggressive
.\shadowdpi.exe -5 --blacklist C:\blocked.txt       # only touch listed domains
.\shadowdpi.exe -9 --verbose                        # verbose diagnostics
.\shadowdpi.exe --help                              # options of this build
```

`-9` blocks QUIC deliberately. If an application needs QUIC and cannot fall
back to TCP, start from `-5` instead.

### Find a working strategy

Hand-picked presets are a starting point, not a guarantee. The strategy scanner
tests presets and option combinations against a list of URLs, using the same
baseline comparison as zapret's blockcheck. It starts one ShadowDPI process per
candidate, waits for `--ready-file`, probes the URLs with `curl.exe`, and
reports the first strategy that recovers every blocked URL without breaking a
working one.

```powershell
.\tools\blockcheck.ps1 -ScanLevel quick
.\tools\blockcheck.ps1 -UrlFile .\blocked.txt -ScanLevel standard -Json result.json
.\tools\blockcheck.ps1 -Urls wikipedia.org,x.com -DnsAddr 77.88.8.8 -DnsPort 1253
```

Edit `tools\blockcheck-urls.txt` or pass `-Urls`. The scanner needs
Administrator rights and `curl.exe`, and it checks for a conflicting ShadowDPI
instance before it starts. Scan levels, result verdicts, JSON fields, and exit
codes are documented in [docs/blockcheck.md](docs/blockcheck.md).

### Turkish presets

Six ready-made recipes for Turkish providers are in
[presets/turkiye](presets/turkiye/README.md), grouped by provider. They wrap the
flags above and keep the DNS endpoints in one editable file, with both console
launchers and a service installer.

The full option reference, alias table, blacklist format, and validation rules
are in [docs/cli.md](docs/cli.md).

## Under the hood

WinDivert intercepts packets at the Windows Filtering Platform and hands them
to user space. ShadowDPI parses the IP and TCP headers, decides whether the
packet belongs to a connection it should touch, rewrites or splits it,
recalculates checksums, and reinjects it. Separate drop filters handle passive
DPI and QUIC.

The implementation is split into a platform-independent core and a thin Windows
layer:

- The core owns CLI parsing, filter construction, HTTP/TLS inspection,
  blacklists, TTL/DNS trackers, and fake payload generation. It is unit-tested
  without WinDivert.
- The Windows layer owns the WinDivert handles, the packet loop, reinjection,
  and service life cycle.
- The packet parser is tested byte-for-byte against
  `WinDivertHelperParsePacket`, including a randomized corpus.

Techniques are standards-compatible: they change packet boundaries, header
spacing, letter case, or TTL, not the meaning of HTTP or TLS. No traffic is
decrypted and no certificate is installed.

## Requirements

- **Windows 10 or 11, x64.** Windows 7 and 8.1 can work if the system trusts
  SHA-2 driver signatures ([KB3033929](https://www.microsoft.com/en-us/download/details.aspx?id=46148)),
  but are not the primary test target.
- **Administrator privileges.** The WinDivert driver is loaded on first run.
- **No runtime install.** The executable uses the static MSVC runtime; the
  release bundles the matching WinDivert DLL and driver.
- **PowerShell 5.1 and `curl.exe`** are needed only for the optional strategy
  scanner. Both ship with Windows 10 and later.

## Build from source (MSVC x64)

ShadowDPI is an **x64-only MSVC project**. There is no 32-bit, MinGW, Clang, or
ARM64 build; the CMake preset selects the Visual Studio x64 toolchain and
configuring another architecture fails on purpose.

You need:

- Visual Studio 2022 or newer with the **Desktop development with C++**
  workload.
- CMake 3.25 or newer.
- A WinDivert 2.2.x distribution.

Point `WINDIVERT_ROOT` at the extracted WinDivert directory, then configure,
build, and test from a Developer PowerShell:

```powershell
$env:WINDIVERT_ROOT = "C:\path\to\WinDivert-2.2.0-D"
cmake --preset msvc-x64
cmake --build --preset msvc-x64-release
ctest --preset msvc-x64-release --output-on-failure
```

The directory must contain `include\windivert.h` and `x64\WinDivert.lib`. The
build copies `WinDivert.dll` and `WinDivert64.sys` next to the executable in
`build\msvc-x64\Release\`.

Tests and the parser benchmark are built by default. To build only the
executable, configure with `-DSHADOWDPI_BUILD_TESTS=OFF`.

### What the tests cover

- `core`: CLI, configuration, filters, HTTP/TLS helpers, blacklist and
  trackers, without WinDivert.
- `windows`: generated filters compiled by WinDivert, parser parity against
  the WinDivert helper, rebase parity, and golden packet-engine cases for
  rewrites, fragmentation, fake packets, DNS redirect, and passive DPI.
- `benchmark`: parser throughput against the WinDivert helper, with a speedup
  floor enforced in optimized builds.

## Windows service

The executable registers itself as the `ShadowDPI` service when started by the
Service Control Manager and shuts down cleanly on a stop request.

```powershell
sc create ShadowDPI binPath= "C:\Tools\ShadowDPI\shadowdpi.exe -9" start= auto
sc description ShadowDPI "Deep Packet Inspection circumvention utility"
sc start ShadowDPI
```

Remove it with:

```powershell
sc stop ShadowDPI
sc delete ShadowDPI
```

## Limits

- **No universal preset.** DPI behavior differs by provider and changes over
  time. `-9` is a starting point, not a guarantee. Run
  [the strategy scanner](docs/blockcheck.md) against your own URL list when the
  presets do not work.
- **IPv6 is implemented but not field-proven.** The benchmark host had no live
  IPv6 DPI path, so IPv6 evasion was not validated against a real provider.
- **QUIC blocking changes behavior.** `-9` intentionally forces TCP fallback.
- **Old Windows 7 images** may reject the WinDivert driver unless SHA-2 support
  is installed.
- **Killer/Intel network cards:** disable *Advanced Stream Detect* in Killer
  Control Center if ShadowDPI fails to start or stalls.
- **Benchmark numbers are host-specific.** The relative CPU advantage is
  robust; the absolute rates are not a promise for your link.

## Credits

The core idea of manipulating TCP segmentation and headers in user space to
defeat DPI on Windows was popularized by **ValdikSS** and the
[GoodbyeDPI](https://github.com/ValdikSS/GoodbyeDPI) project. ShadowDPI follows
that concept as an independent C++20 implementation; it contains no GoodbyeDPI
code or assets. Credit where it is due.

Packet interception is provided by **WinDivert** by
[basil00](https://github.com/basil00/Divert), which is a substantial part of
what makes this possible. See [NOTICE](NOTICE) for licensing details.

## License

ShadowDPI is released under the [GNU General Public License, version 3](LICENSE).
The complete source for each release is the matching tag in this repository.

WinDivert is redistributed unmodified under the LGPLv3. See [NOTICE](NOTICE)
and [third_party/WinDivert/LICENSE](third_party/WinDivert/LICENSE).
