# Tools

| File | Purpose |
| --- | --- |
| `blockcheck.ps1` | Tests ShadowDPI presets and option combinations against a URL list and reports a recommended command line. |
| `blockcheck-urls.txt` | Default URL list for the scanner. Edit it or pass `-Urls` instead. |

Run the scanner from an Administrator PowerShell:

```powershell
.\tools\blockcheck.ps1 -ScanLevel quick
```

The full guide, including scan levels, DNS options, result verdicts, JSON
output, and exit codes, is in [../docs/blockcheck.md](../docs/blockcheck.md).
