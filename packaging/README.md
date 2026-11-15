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

## Winget

Winget is deliberately not provided yet, because the current release layout does
not fit any installer type safely:

- `portable` extracts a single file, so `WinDivert.dll` would be missing and the
  executable would fail to start.
- `zip` with a nested `portable` installer has the same problem.
- `script` points at one file, but `install.ps1` expects the other release files
  next to it.

The clean fix is a real installer: an MSI (WiX) or a signed `setup.exe` that
runs the existing install logic and declares `ElevationRequirement:
elevationRequired`. With that in place:

1. Generate manifests with `wingetcreate new <installer-url>`.
2. Set `PackageIdentifier` to `amcique.ShadowDPI`, `License: GPL-3.0-only`,
   `ShortDescription`, and `Moniker: shadowdpi`.
3. Add `Tags: dpi, censorship, windivert, goodbye dpi, tls, http`.
4. Test with `winget install --manifest <dir>`.
5. Submit with `wingetcreate submit <dir>`.

Until then, point users at the release zip. A half-working Winget package costs
more trust than a missing one.

## Both channels

Whatever the channel, the installed layout must keep these four files together:

```
shadowdpi.exe
WinDivert.dll
WinDivert64.sys
install.ps1
```

`docs/`, `tools/`, `LICENSE`, `NOTICE`, `README.md`, `README.ru.md`,
`README.tr.md`, and `SECURITY.md` are documentation; the scanner and installer
reference `docs/` and `tools/` by relative path, so keep them too.
