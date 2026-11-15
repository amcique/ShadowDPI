# Changelog

All notable changes to ShadowDPI are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/).

## [0.3.0] — 2026-11-15

First public release. This is the version described by the README and the
benchmarks in `docs/performance.md`.

### Added

- User-space DPI circumvention on top of WinDivert, with additive presets
  (`-1` … `-9`) for HTTP Host tricks, TLS splitting, native fragmentation, fake
  packets with a wrong sequence number or bad checksum, automatic fake TTL, and
  QUIC blocking.
- Strategy scanner (`install.ps1 -Action scan`) that starts one process per
  candidate, probes a URL list with `curl.exe`, and reports the first strategy
  that recovers every blocked URL without breaking a working one. Level,
  JSON output, custom URL file, and DNS resolver selection are documented in
  `docs/scan.md`.
- Combined installer and scanner in `install.ps1`: install, upgrade,
  reconfigure, uninstall, and scan, plus the same actions as an interactive
  menu.
- Auto-start Windows service mode with configurable name, arguments, and start
  type; the executable registers itself when started by the Service Control
  Manager.
- DNS redirect for IPv4 and IPv6 resolvers, with transaction-ID, question, and
  resolver-endpoint validation.
- Blacklist support to scope host-aware tricks to selected domains, with a
  documented note that the SYN/ACK window-size step is necessarily global.
- `--ready-file` for reliable process-start synchronisation in scripts and the
  scanner.
- Parser benchmark with an enforced speedup floor in optimized builds.

### Changed

- Preset scripts are gone; presets are CLI flags, and the README renders the
  exact expansion of each one.
- `-1` and `-2` now enable native fragmentation so they match their documented
  expansions.
- The release archive bundles `WinDivert.dll` and `WinDivert64.sys` and no
  longer requires a separate download.
- Documentation restructured into `docs/cli.md`, `docs/install.md`,
  `docs/scan.md`, and `docs/performance.md`.

### Fixed

All 20 findings from the pre-release source review are fixed; the report is in
[`docs/reviews/2026-11-source-review.md`](docs/reviews/2026-11-source-review.md).

- A failed WinDivert receive is now reported as a failure instead of a clean
  exit, so the service cannot claim success while protection is off.
- The passive-DPI filter now applies the intended IP-ID restriction instead of
  dropping every inbound RST on ports 80 and 443.
- The stop callback no longer races with filter setup or outlives `Application`.
- Service status updates are serialised, and only
  `ERROR_FAILED_SERVICE_CONTROLLER_CONNECT` falls back to console mode.
- DNS redirect rejects spoofed responses with a mismatched transaction ID,
  question, or source.
- The TTL and DNS trackers are capped and cleaned up incrementally, so a
  spoofed SYN/ACK or DNS flood cannot grow memory without bound.
- WinDivert send failures are reported, rate-limited, and abort the remaining
  fragments of the same datagram.
- Native fragmentation no longer duplicates FIN/URG/PSH, so a peer cannot close
  the connection on the first fragment.
- The extra-port filter clause requires an acknowledgement and a payload and
  respects port direction.
- HTTP header lookup is case-insensitive and limited to the header block, so
  lowercase `host:` works and body text cannot be mistaken for a header.
- SNI extraction continues past a malformed candidate instead of giving up, so
  a decoy cannot suppress a real SNI.
- `copy_packet` clamps the logical length to the destination capacity.
- QUIC blocking matches both QUIC v1 and v2.
- The application manifest declares Windows 7 support, matching the README.

### Security

- `shadowdpi.exe` no longer links `WinDivert.lib`. The DLL is loaded at runtime
  with explicit application-and-system32 search flags plus `GetProcAddress`,
  closing a DLL-planting path that the previous static import allowed before
  `main()` ran. `dumpbin /DEPENDENTS` confirms there is no static import.
- `dnsapi.dll` is loaded from `System32`, and the process DLL search path is
  restricted on startup.
- Added `SHADOWDPI_WARNINGS_AS_ERRORS` and
  `SHADOWDPI_REQUIRE_WINDIVERT_RUNTIME` build options; CI builds and tests with
  strict settings.
- Added `/guard:cf` and `/sdl` to the executable and test targets.

[0.3.0]: https://github.com/amcique/ShadowDPI/releases/tag/v0.3.0
