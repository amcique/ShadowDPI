# Strategy scanner

`install.ps1 -Action scan` tests many ShadowDPI option combinations against a
list of URLs and reports which ones restore access. It is a Windows equivalent
of the blockcheck idea from [zapret](https://github.com/bol-van/zapret):
compare a baseline with each candidate, probe every URL, and keep the
strategies that work. The implementation is independent PowerShell code and
contains no zapret code.

The scanner never changes the system DNS settings. It starts one
`shadowdpi.exe` process per candidate, waits for the filters to open, probes the
URL list, and stops the process again. If an installed ShadowDPI service is
running, the scanner stops it before the baseline and starts it again when the
scan ends.

## Requirements

- Windows 10 or 11, x64.
- Administrator rights, because WinDivert needs to load its driver.
- Windows PowerShell 5.1 or PowerShell 7.
- `curl.exe`, which ships with Windows 10 version 1803 and later.
- `shadowdpi.exe`, `WinDivert.dll`, and `WinDivert64.sys` next to the script,
  in the installed folder, or in the usual build output folders.

A console `shadowdpi.exe` process must not be running while the baseline is
measured. The scanner stops an installed service itself and restarts it
afterward.

## Quick start

From the extracted release folder or from the installed folder:

```powershell
.\install.ps1 -Action scan -ScanLevel quick
```

The default URL list is `tools\scan-urls.txt`. Edit it before scanning, or pass
URLs directly:

```powershell
.\install.ps1 -Action scan -Urls https://example.com/,https://example.org/
```

A bare host is treated as `https://host/`:

```powershell
.\install.ps1 -Action scan -Urls wikipedia.org,x.com -ScanLevel standard
```

Running `install.ps1` without arguments opens the menu, where scanning is one
of the choices.

Unless you pass `-DnsAddr`, `-DnsV6Addr`, or `-DnsResolver`, the script asks
which DNS resolver to use:

```
DNS resolver for the scan
  1. Cloudflare 1.1.1.1 (recommended)
  2. Google 8.8.8.8
  3. Other
  4. Do not specify (use the system resolver)
```

If the provider poisons DNS answers, choose Cloudflare, Google, or `Other`.
When a resolver is selected, every candidate also receives `--dns-addr` and
`--dns-port`, and an extra `DNS redirect only` candidate is added:

```powershell
.\install.ps1 -Action scan -UrlFile .\blocked.txt `
    -DnsResolver cloudflare -ScanLevel full -Json result.json
```

Use `-DnsResolver system` to skip the question and keep the Windows resolver,
or pass an explicit address when you need a custom port:

```powershell
.\install.ps1 -Action scan -UrlFile .\blocked.txt `
    -DnsAddr 77.88.8.8 -DnsPort 1253 -ScanLevel full
```

The question is skipped when input or output is redirected, and during
`-DryRun`, so unattended runs fall back to the system resolver.

## How a scan works

1. Preflight checks the executable, the WinDivert files, curl, Administrator
   rights, and any running ShadowDPI process. It stops a running installed
   service and records it for restart, then starts
   `shadowdpi.exe --debug-exit --ready-file` once to confirm that the build
   supports the readiness signal.
2. The baseline probes every URL with ShadowDPI stopped. Probes use a browser
   HTTP version negotiation to curl; forced TLS variants pin HTTP/1.1.
3. Probes that fail at baseline become targets. A strategy passes only when
   every target is reachable and no baseline-working probe regresses.
4. Each candidate starts `shadowdpi.exe` with `--ready-file`. The script waits
   for the file instead of sleeping for a fixed time, then probes every URL.
5. The process is stopped, the next candidate starts, and the results are
   ranked. The first fully passing strategy is printed as the recommended
   command line. If the scanner stopped an installed service, it starts that
   service again when the scan ends.

The scanner removes its temporary folder with the ready files and process logs
at the end. Use `-KeepArtifacts` to keep it for inspection.

## Scan levels

| Level | What it tests | Typical use |
| --- | --- | --- |
| `quick` | Presets `-1` through `-9` | A fast first scan |
| `standard` | Presets plus fixed TTL values, fragmentation sizes, Host tricks, wrong SEQ/checksum, and reverse-order variants | The normal choice |
| `full` | Standard plus TTL and fragment sweeps, auto-TTL ranges, resend counts, generated fakes, and per-host fake SNI payloads | Exhaustive search |

The full matrix has roughly 90 candidates on a single URL. Each candidate
starts and stops a process, so the runtime depends mostly on the number of
blocked probes and `-TimeoutSec`. `-MaxStrategies` limits the matrix when you
want a shorter run.

## Options

| Option | Meaning |
| --- | --- |
| `-Urls <string[]>` | URLs to test. Overrides the URL file. |
| `-UrlFile <path>` | File with one URL per line. Blank lines, `#`, and `;` are ignored. |
| `-Exe <path>` | Path to `shadowdpi.exe`. By default the script prefers the installed copy, then the copy next to the script, then the build output folders. |
| `-InstallPath <path>` | Installed folder used to find the executable and the running service. Defaults to the folder recorded in `install.json`. |
| `-ScanLevel quick\|standard\|full` | Candidate matrix size. Defaults to `standard`. |
| `-Protocols auto\|tls12\|tls13\|all` | HTTPS probe mode. `auto` uses curl's normal negotiation. `all` probes TLS 1.2 and TLS 1.3 separately. |
| `-IpVersion 4\|6\|auto` | Address family passed to curl. Defaults to `4`. |
| `-TimeoutSec <n>` | Maximum time for one curl probe. Defaults to `5`. |
| `-StartupTimeoutSec <n>` | Maximum wait for the ready file. Defaults to `10`. |
| `-Repeats <n>` | Probe attempts. `0` means 1 for quick and standard, 2 for full. |
| `-DnsResolver ask\|cloudflare\|google\|system` | DNS choice. `ask` shows the menu in an interactive session, `cloudflare` uses 1.1.1.1, `google` uses 8.8.8.8, and `system` keeps the Windows resolver. Defaults to `ask`. |
| `-DnsAddr <ipv4>` | Skip the menu and add `--dns-addr` to every candidate. |
| `-DnsPort <port>` | Port for `-DnsAddr`, `-DnsResolver cloudflare`, and `-DnsResolver google`. Defaults to `53`. |
| `-DnsV6Addr <ipv6>` | Skip the menu and add `--dnsv6-addr` to every candidate. |
| `-DnsV6Port <port>` | Port for `-DnsV6Addr`. Defaults to `53`. |
| `-Json <path>` | Write the complete results as UTF-8 JSON without a BOM. |
| `-ProgressFile <path>` | Append one JSON line per completed strategy, for live progress in the GUI. |
| `-MaxStrategies <n>` | Limit the number of candidates. `0` keeps all. |
| `-SettleMs <n>` | Delay after the ready file appears before probing. Defaults to `200`. |
| `-NoDnsFlush` | Skip `ipconfig /flushdns` before the baseline. |
| `-DryRun` | Print the candidate matrix and exit without touching the network. |
| `-KeepArtifacts` | Keep ready files and process logs in the temporary folder. |

## Reading the results

Each candidate gets one of these verdicts:

| Verdict | Meaning |
| --- | --- |
| `pass` | Every baseline-blocked probe is reachable and no working probe regressed. |
| `partial` | At least one blocked probe was restored, but not all of them, or a working probe regressed. |
| `fail` | No blocked probe was restored. |
| `regression` | The baseline had no blocked probes, but this candidate broke a working one. |
| `error` | ShadowDPI did not start, or it exited during the probes. The stderr text is shown. |

A probe is counted as reachable when curl exits with status 0, the HTTP status
is not 400, 403, 451, or 5xx, and any redirect stays on the same host or a
subdomain. HTTP 400 is reported separately because it often means the server
received fake packets. HTTP 403, 451, and 5xx are reported as possible block
pages or server errors.
Redirects to another host are reported as suspicious, the same idea used by
zapret's blockcheck.

`-Protocols all` forces TLS 1.2 and TLS 1.3 separately. A server that does not
support one of them can keep a strategy at `partial`; use the default `auto`
when you only care about the protocol the client and server negotiate.

## JSON output

`-Json` writes one object with the executable and curl versions, the scan
settings, the URL and probe lists, the baseline results, every strategy result
with its per-probe details, and the recommendation:

```json
{
  "recommended": {
    "commandLine": "\"C:\\Tools\\ShadowDPI\\shadowdpi.exe\" -5 --fake-ttl 5",
    "targetsPassed": 2,
    "targetTotal": 2,
    "regressions": 0
  }
}
```

The JSON file is written even when no strategy passes. In that case
`recommended` is `null`.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | The scan completed and either a passing strategy was found or the baseline already reached every URL. |
| `1` | A preflight check or the scan itself failed. |
| `2` | The scan completed but no strategy restored every blocked probe without regressions. |

## Limits

- The scanner changes state only through `shadowdpi.exe` options. It does not
  test browser-specific settings, proxies, or VPNs.
- QUIC is not probed because the Windows curl build may not include HTTP/3.
  Candidates that block QUIC still affect applications, but the scanner only
  measures TCP results.
- A certificate error cannot be fixed by ShadowDPI. Such a URL stays a target
  and makes every strategy look worse; remove it from the list if it is not
  part of the DPI problem.
- Results are specific to the tested network and time. Re-run the scan when the
  provider changes its behavior.
