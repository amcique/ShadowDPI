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

[`winget/amcique.ShadowDPI/0.3.0`](winget/amcique.ShadowDPI/0.3.0) holds a
validated three-file manifest for
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs).

It uses `InstallerType: zip` with `NestedInstallerType: portable`. Winget
extracts the whole archive, so `WinDivert.dll` and `WinDivert64.sys` stay next to
`shadowdpi.exe`, and the command alias `shadowdpi` points at the executable. The
v0.3.0 manifest was tested with winget 1.29.380 against the published release
zip, including a full install and a `shadowdpi --version` run.

### Test a manifest locally

Local manifest installs are disabled by default. Enable them once from an
elevated shell:

```powershell
winget settings --enable LocalManifestFiles
```

Validate and install from a normal, non-elevated shell. A portable package
installs for the current user, and an elevated shell cannot uninstall it:

```powershell
winget validate --manifest .\packaging\winget\amcique.ShadowDPI\0.3.0
winget install --manifest .\packaging\winget\amcique.ShadowDPI\0.3.0
winget uninstall --name ShadowDPI --exact
```

Running `shadowdpi.exe` itself needs Administrator, so launch it from an
elevated shell; the application manifest requests elevation even for `--version`.

Turn the setting off afterwards:

```powershell
winget settings --disable LocalManifestFiles
```

### Update for a new release

1. Copy the previous version folder to a folder named after the new version.
2. Update `PackageVersion` in all three files, plus `InstallerUrl` and
   `InstallerSha256` in the installer file.
3. Point `ReleaseNotesUrl` and `LicenseUrl` at the new tag.
4. Validate and test as above, then submit:

```powershell
wingetcreate submit .\packaging\winget\amcique.ShadowDPI\<version>
```

Winget installs per user and does not register the service. Users who want the
auto-start service should run `install.ps1` from the package folder as
Administrator, or use the release zip.

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
