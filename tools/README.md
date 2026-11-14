# Tools

| File | Purpose |
| --- | --- |
| `blockcheck.ps1` | Tests ShadowDPI presets and option combinations against a URL list and reports a recommended command line. |
| `blockcheck-urls.txt` | Default URL list for the scanner. Edit it or pass `-Urls` instead. |

Run the scanner from an Administrator PowerShell:

```powershell
.\tools\blockcheck.ps1 -ScanLevel quick
```

The scanner asks for a DNS resolver in an interactive session. Pass
`-DnsResolver system` to skip the question, or `-DnsAddr` for a custom address.

The full guide, including scan levels, DNS options, result verdicts, JSON
output, and exit codes, is in [../docs/blockcheck.md](../docs/blockcheck.md).
