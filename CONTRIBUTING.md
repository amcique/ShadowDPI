# Contributing to ShadowDPI

Thanks for taking the time to contribute. Bug reports, tuning reports from
providers that behave unusually, and pull requests are all welcome.

Please read the [Code of Conduct](CODE_OF_CONDUCT.md), adapted from Contributor
Covenant 2.1. In short: be civil and assume good faith. Security issues go
through [SECURITY.md](SECURITY.md), not the public tracker.

## Before you open an issue

- **"ShadowDPI does not unblock my site."** Run
  `.\install.ps1 -Action scan -ScanLevel standard` first and include the result.
  Presets are a starting point, not a guarantee, and provider behaviour changes.
- **"My antivirus / SmartScreen complains."** Expected for an unsigned binary
  that ships a kernel driver. Verify the release SHA-256 sums; see the README.
- **"It does not work on ARM64 / 32-bit / Linux."** By design. The project is
  x64 Windows with MSVC only.
- Attach `--verbose` output and your Windows build number when reporting
  crashes or stalls.

## Building and testing

Requirements are listed in the README. From a Developer PowerShell:

```powershell
$env:WINDIVERT_ROOT = "C:\path\to\WinDivert-2.2.0-D"
cmake --preset msvc-x64
cmake --build --preset msvc-x64-release
ctest --preset msvc-x64-release --output-on-failure
```

Before opening a pull request, also build with the strict settings so CI
warnings do not surprise you:

```powershell
cmake --preset msvc-x64 `
  -DSHADOWDPI_WARNINGS_AS_ERRORS=ON `
  -DSHADOWDPI_REQUIRE_WINDIVERT_RUNTIME=ON
cmake --build --preset msvc-x64-release
ctest --preset msvc-x64-release --output-on-failure
```

## What a good pull request looks like

- **One concern per pull request.** A behaviour change plus a refactor is two
  pull requests.
- **Tests.** Logic in `shadowdpi_core` must be unit-tested without WinDivert.
  Packet-level changes belong in the golden packet-engine cases in `tests/`.
- **No new dependencies.** The current build depends only on the MSVC runtime,
  WinDivert, and the Windows SDK. Adding a library needs a strong reason.
- **Match the surrounding code.** C++20, `sdp` namespace, 4-space indent,
  `snake_case` for functions and variables, `kConstant` for constants, and
  comments only where the reason is not obvious from the code.
- **Keep the docs true.** If you change a flag, a preset expansion, a filter, or
  a limit, update `README.md` and the relevant file under `docs/`. A pull
  request that makes documentation wrong will be sent back.
- **No drive-by formatting.** It makes review harder and the history noisier.

## Commit messages

Short imperative subject line, then a body explaining *why* when the change is
not obvious. Example:

```
Clamp the copied length in copy_packet

An oversized source packet left the descriptor larger than its backing
storage. No current caller triggers it, but the helper should not be able to
produce an invalid descriptor at all.
```

## Review and licensing

By submitting a pull request you agree that your contribution is licensed under
the [GNU General Public License, version 3](LICENSE), like the rest of the
project. Do not paste code from projects with incompatible licences. If you
port an idea rather than code, say so in the description.
