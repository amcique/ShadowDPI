# Tools

| File | Purpose |
| --- | --- |
| `scan-urls.txt` | Default URL list for the strategy scanner. Edit it or pass `-Urls` instead. |

The installer and the strategy scanner are one script at the release root.
From an elevated PowerShell:

```powershell
.\install.ps1
```

The menu offers install, upgrade, reconfigure, uninstall, and strategy
scanning. The scan can also be started directly:

```powershell
.\install.ps1 -Action scan -ScanLevel quick
```

The scanner asks for a DNS resolver in an interactive session. Pass
`-DnsResolver system` to skip the question, or `-DnsAddr` for a custom address.

The installer copies a fixed payload from the release folder:
`shadowdpi.exe`, `WinDivert.dll`, `WinDivert64.sys`, `LICENSE`, `NOTICE`,
`README.md`, `install.ps1`, and the `docs`, `tools`, and `resources` folders.
Add new top-level release files to `Copy-InstallPayload` in `install.ps1`.
Files removed between releases are not deleted from an existing install.

The full guide, including installer actions, scan levels, DNS options, result
verdicts, JSON output, and exit codes, is in
[../docs/install.md](../docs/install.md) and [../docs/scan.md](../docs/scan.md).
