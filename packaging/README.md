# Packaging

Optional package-manager manifests. The GitHub release zip remains the source of
truth; these files only describe how to wrap it for other installers.

## Scoop

[`scoop/shadowdpi.json`](scoop/shadowdpi.json) is a complete manifest. It
extracts the release zip into its own directory so `shadowdpi.exe`,
`WinDivert.dll`, and `WinDivert64.sys` stay side by side, which is what the
runtime loader requires.

The `hash` field holds the SHA-256 of the current release archive. For a new
release, compute it and replace the value:

```powershell
(Get-FileHash .\shadowdpi-<version>-x64.zip -Algorithm SHA256).Hash.ToLower()
```

Then test locally:

```powershell
scoop install .\packaging\scoop\shadowdpi.json
shadowdpi.exe --version
scoop uninstall shadowdpi
```

Scoop installs per user, so it cannot register the auto-start service. Run
`install.ps1` from the installed directory as Administrator for that, or use the
regular release zip.

To publish, add the manifest to a bucket repository (your own, or a public one
such as `ScoopInstaller/Extras` after the project has a release history).

## Installed layout

The installed layout must keep these four files together:

```
shadowdpi.exe
WinDivert.dll
WinDivert64.sys
install.ps1
```

`docs/`, `tools/`, `LICENSE`, `NOTICE`, `README.md`, `README.ru.md`,
`README.tr.md`, and `SECURITY.md` are documentation; the scanner and installer
reference `docs/` and `tools/` by relative path, so keep them too.
