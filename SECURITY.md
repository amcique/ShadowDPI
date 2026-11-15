# Security policy

ShadowDPI runs with Administrator privileges and handles raw packets, so a bug
in it can affect every connection on the machine. Reports are taken seriously.

## Reporting a vulnerability

**Do not open a public issue for a security problem.**

Use GitHub's private vulnerability reporting:

1. Open the [Security tab](https://github.com/amcique/ShadowDPI/security).
2. Choose **Report a vulnerability**.
3. Include the ShadowDPI version, Windows build, preset or command line, and a
   minimal reproduction.

If private reporting is unavailable, open a public issue that says only *"I have
a security report, please contact me"* and nothing else. A maintainer will move
the conversation to a private channel.

### What to expect

| Stage | Target |
|---|---|
| Acknowledgement | 5 days |
| Initial assessment | 14 days |
| Fix or mitigation plan | 30 days |

This is a volunteer project, so these are targets rather than guarantees. You
will be credited in the release notes unless you ask otherwise.

## Supported versions

Only the latest release receives security fixes. The `main` branch is the
development branch and may be broken at any time.

| Version | Supported |
|---|---|
| 0.3.x | Yes |
| < 0.3 | No |

## In scope

- Memory-safety bugs in packet parsing, fragmentation, or reinjection.
- Out-of-bounds reads or writes reachable from network input.
- Privilege escalation through DLL or driver loading, or through the installer.
- Denial of service reachable from unauthenticated traffic, for example
  unbounded memory growth in the TTL or DNS trackers.
- Filter or blacklist bypasses that cause traffic to be modified when the user
  scoped it away.
- Installer path traversal, unsafe file writes, or service configuration
  injection.
- Ways to make ShadowDPI drop or corrupt traffic it should pass through.

## Out of scope

- **DPI evasion failing against your provider.** That is a tuning problem, not a
  vulnerability. Use [the strategy scanner](docs/scan.md) and open a normal
  issue.
- **Antivirus or SmartScreen warnings.** The release binaries are not
  Authenticode-signed and WinDivert is a kernel driver, so reputation prompts
  are expected. Verify the published SHA-256 sums instead; see the README.
- **"This tool can be used to bypass network policy."** ShadowDPI is a network
  research and censorship-circumvention tool. Its intended use is described in
  the README.
- **Local attackers who already have Administrator.** ShadowDPI is an
  Administrator-level component by design; an Administrator can replace the
  binary or the service definition regardless.
- Vulnerabilities in WinDivert itself. Report those
  [upstream](https://github.com/basil00/Divert); we will still ship a fixed
  version promptly.
- Missing hardening flags with no demonstrated impact. These are welcome as
  normal pull requests.

## Security design notes

These are the properties a report should be measured against.

- **Loading.** `shadowdpi.exe` does not import `WinDivert.dll` statically. The
  DLL is resolved at runtime with
  `LoadLibraryExA(..., LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)`
  and `GetProcAddress`, and the process search path is restricted before any
  other module is loaded. `dumpbin /DEPENDENTS` shows no static WinDivert
  import.
- **No TLS interception.** ShadowDPI never decrypts traffic, terminates TLS, or
  installs a certificate. It changes packet boundaries, header casing, TTL, and
  sequence or checksum fields only.
- **DNS redirect validation.** Redirected DNS responses are accepted only when
  the transaction ID, the question, and the resolver endpoint all match a query
  that ShadowDPI actually forwarded.
- **Bounded state.** The TTL and DNS trackers have hard entry caps and evict
  rather than grow without limit under a spoofed-packet flood.
- **No telemetry.** ShadowDPI makes no outbound connections of its own.
- **Verifiable builds.** Releases are built in GitHub Actions from the tagged
  commit, the workflow pins the WinDivert 2.2.0-D SHA-256, and every archive is
  published with a build-provenance attestation. Verify one with
  `gh attestation verify`, as the release notes describe. The build is not
  bit-for-bit reproducible: the runner image and the MSVC version follow the
  hosted image rather than a pinned toolchain, so rebuilding produces a working
  binary with a different hash.

The full pre-release review, including issues found and how they were fixed, is
in [`docs/reviews/2026-11-source-review.md`](docs/reviews/2026-11-source-review.md).
