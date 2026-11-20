# Installer

`install.ps1` installs, upgrades, reconfigures, removes, and scans ShadowDPI.
It ships at the root of every release and is also copied into the installed
folder. With no arguments it opens an interactive menu.

The installer uses the files from the release folder it is run from. It does
not download anything.

## Requirements

- Windows 10 or 11, x64.
- Administrator rights for install, upgrade, reconfigure, uninstall, and scan.
- Windows PowerShell 5.1 or PowerShell 7.
- The release files next to `install.ps1`: `shadowdpi.exe`, `WinDivert.dll`,
  and `WinDivert64.sys`. The documentation and tools folders are copied when
  they are present.

## Quick start

Open an elevated PowerShell in the extracted release folder and run:

```powershell
.\install.ps1
```

The menu offers Install, Upgrade, Reconfigure, Repair, Uninstall, Scan, and
Exit, depending on what is already installed. Choosing Install copies the files to
`%ProgramFiles%\ShadowDPI`, registers an auto-start service named `ShadowDPI`
with `-9`, and starts the service.

The same install can be started without the menu:

```powershell
.\install.ps1 -Action install
```

To install without a service:

```powershell
.\install.ps1 -Action install -NoService
```

To install without starting the service after it is registered:

```powershell
.\install.ps1 -Action install -NoStart
```

## MSI installer

Each release also publishes `shadowdpi-<version>-x64.msi`. It is an alternative
to the PowerShell installer, not a replacement: the zip and `install.ps1`
continue to work, including for Scoop installs and upgrades. The MSI is unsigned,
so Windows may show an Unknown publisher warning and SmartScreen may warn. Verify
the download with the published SHA-256 manifest or a GitHub attestation, as
described in [SECURITY.md](../SECURITY.md).

The MSI installs to `%ProgramFiles%\ShadowDPI`, keeps the WinDivert files beside
the executables, registers the auto-start service, adds Start menu shortcuts,
and creates an Add/Remove Programs entry named `ShadowDPI <version>`. The setup
dialog asks for the service name, the service arguments, the start mode
(Automatic, Manual, or Disabled), and whether to start the service after setup.
Defaults are `ShadowDPI`, `-9`, Automatic, and start after setup.

Upgrade by running a newer MSI. The service settings are stored in the registry
under `HKLM\SOFTWARE\ShadowDPI`, so the new MSI reads them back and keeps them.
Repair and Remove are available from Windows Settings. For unattended use:

```powershell
msiexec /i shadowdpi-0.5.0-x64.msi /qn /norestart `
  INSTALLFOLDER="D:\Tools\ShadowDPI" SERVICENAME=ShadowDPI `
  SERVICEARGS="-9" SERVICESTARTMODE=auto STARTATSERVICE=1
msiexec /x shadowdpi-0.5.0-x64.msi /qn /norestart
```

Uninstall stops and deletes the service, removes the registry settings and
shortcuts, and removes the install folder. An MSI install refuses to proceed if
a legacy Add/Remove Programs entry with Publisher `amcique` exists. Run
`install.ps1 -Action uninstall -Force` from the old install folder first.

## Actions

| Action | What it does |
| --- | --- |
| `menu` | Opens the interactive menu. This is the default. |
| `install` | Installs from the current folder. If ShadowDPI is already installed, it reinstalls the files and preserves the existing service settings. |
| `upgrade` | Requires an existing installation. Replaces the program files and preserves the service name, arguments, start type, and running state. |
| `reconfigure` | Requires an existing installation. Changes the service settings without replacing program files. |
| `repair` | Requires an existing installation. Re-copies the files from this release folder and restarts the service if it was running. Service settings are preserved. |
| `uninstall` | Stops and deletes the service, then removes the install folder. |
| `scan` | Runs the strategy scanner described in [scan.md](scan.md). |

`-Action` defaults to `menu`, so double-clicking or running the script without
arguments is safe.

## Automation and logging

`-Silent` suppresses console output and confirmation prompts. Combine it with
`-Force` for unattended uninstalls:

```powershell
.\install.ps1 -Action install -Silent -Force
.\install.ps1 -Action uninstall -Silent -Force
```

`-LogPath` writes a timestamped log. By default the log goes to
`%ProgramData%\ShadowDPI\logs\install.log`:

```powershell
.\install.ps1 -Action upgrade -LogPath C:\Temp\shadowdpi-install.log
```

When a `*.sha256` manifest is found next to `install.ps1` or in the release
folder, the installer verifies `WinDivert.dll` and `WinDivert64.sys` before
copying them. A mismatch aborts the install. Without a manifest the check is
skipped with a warning.

A successful install registers an Add/Remove Programs entry named ShadowDPI,
so uninstall also works from Windows Settings. The entry's uninstall command
uses `-Action uninstall -Force -Silent`, and the QuietUninstallString variant
adds `-Quiet`.

## Install layout

The default install folder is `%ProgramFiles%\ShadowDPI`. The installer copies:

- `install.ps1`
- `shadowdpi.exe`
- `shadowdpi-gui.exe`
- `WinDivert.dll`
- `WinDivert64.sys`
- `LICENSE`
- `NOTICE`
- `README.md`
- `docs` and `tools`, when they exist in the release
- `resources`, when it exists in the release

It writes `install.json` in the install folder. That file records the version,
the source folder, the install time, the service settings, and the installed
payload. The installer uses it to find an existing installation, preserve its
configuration, and remove files that a later release no longer contains.

At startup, `shadowdpi.exe` checks that `WinDivert64.sys` sits next to the loaded
`WinDivert.dll` and stops with an error if it is missing. Copying only
`shadowdpi.exe` out of the archive is not enough. If the two files come from
different WinDivert releases, WinDivert reports the mismatch when the driver is
opened, and the error explains how to remove the loaded driver.

A custom folder can be selected with `-InstallPath`:

```powershell
.\install.ps1 -Action install -InstallPath 'D:\Tools\ShadowDPI'
```

The folder must not be a drive root, a protected system folder, or a folder
inside the extracted release. Pass the same `-InstallPath` to later actions on
a custom install. Without it, the installer looks in
`%ProgramFiles%\ShadowDPI` and then at the registered service path.

## Service settings

The default service configuration is:

| Setting | Default |
| --- | --- |
| Name | `ShadowDPI` |
| Arguments | `-9` |
| Start mode | Automatic |
| Start after install | Yes |

Change them on a new install:

```powershell
.\install.ps1 -Action install -ServiceName ShadowDPI -ServiceArgs '-5'
```

The service binary path is written with the executable quoted, for example:

```text
"C:\Program Files\ShadowDPI\shadowdpi.exe" -5
```

Reinstall and upgrade preserve the service name, arguments, start mode, and
whether the service was running. If the service was running, it is stopped for
the file update and started again afterward. If it was stopped, it stays
stopped. `-NoStart` forces it to stay stopped.

The installer also configures recovery. If the service exits unexpectedly,
Windows restarts it after 5 seconds, then 15, then 60, and resets the count
after a day of uptime. A stop requested through the Service Control Manager is
not restarted. To turn recovery off, run this from an elevated Command Prompt:

```text
sc.exe failure ShadowDPI reset= 0 actions= ""
```

If a service with the requested name already exists and points to a different
folder, a new install stops with an error instead of changing that service.
Choose another name with `-ServiceName` or remove the existing service first.

`-NoService` has different effects by action:

- On a new install, it skips service creation.
- On reinstall or upgrade, an existing service is still stopped for the file
  update and started again afterward, so its configuration is preserved.
- On reconfigure, it removes the service.

## Reconfigure

`reconfigure` changes the service without replacing `shadowdpi.exe`:

```powershell
.\install.ps1 -Action reconfigure -ServiceArgs '-5'
```

In an interactive session it asks whether to use a service, then asks for the
name, arguments, automatic start, and whether to start it now. In a
non-interactive session it uses the current settings plus any explicit
parameters.

Remove the service while keeping the installed files:

```powershell
.\install.ps1 -Action reconfigure -NoService -Force
```

## Upgrade

Extract the new release and run the installer from that new folder:

```powershell
.\install.ps1 -Action upgrade
```

The installer checks for an existing installation, reads `install.json`, stops
the service if needed, replaces the program files, removes files that the new
release no longer contains, restores the service settings, and starts the
service again if it was running. It does not run `-Action upgrade` from the
installed folder because that folder already contains the old files. Use a
freshly extracted release folder as the source.

## Uninstall

```powershell
.\install.ps1 -Action uninstall
```

The installer asks for confirmation in an interactive session. It stops and
deletes the service, removes the install folder, and leaves the downloaded
release folder untouched. Non-interactive uninstall requires `-Force`:

```powershell
.\install.ps1 -Action uninstall -Force
```

## Parameters

| Parameter | Meaning |
| --- | --- |
| `-Action menu\|install\|upgrade\|reconfigure\|uninstall\|scan` | Action to run. Defaults to `menu`. |
| `-InstallPath <path>` | Install folder. Defaults to `%ProgramFiles%\ShadowDPI`. A registered service can also identify the folder. Pass this parameter to later actions on a custom install. |
| `-ServiceName <name>` | Windows service name. Defaults to `ShadowDPI` on a new install and preserves the existing name otherwise. |
| `-ServiceArgs <args>` | Arguments stored in the service binary path. Defaults to `-9` on a new install and preserves the existing value otherwise. |
| `-NoService` | Skip service creation on a new install, preserve an existing service on reinstall or upgrade, or remove it on reconfigure. |
| `-NoStart` | Leave the service stopped after install, upgrade, or reconfigure. |
| `-SourcePath <path>` | Folder containing `shadowdpi.exe` and the WinDivert files. Defaults to the folder containing `install.ps1`. |
| `-Quiet` | Do not ask configuration questions. Use explicit parameters and defaults. |
| `-Silent` | Suppress console output and confirmation prompts. Combine with `-Force` for unattended uninstalls. |
| `-LogPath <path>` | Write a timestamped install log. Defaults to `%ProgramData%\ShadowDPI\logs\install.log`. |
| `-Force` | Skip confirmation prompts. Required for non-interactive uninstall. |

All scanner parameters, such as `-ScanLevel`, `-Urls`, `-UrlFile`, `-Exe`,
`-DnsResolver`, `-DnsAddr`, `-Json`, and `-ProgressFile` are accepted together
with `-Action scan` or the menu scan choice.

## install.json

A successful install writes a file like this:

```json
{
  "product": "ShadowDPI",
  "schemaVersion": 1,
  "version": "0.5.0",
  "versionText": "ShadowDPI 0.5.0 (C++20)",
  "installPath": "C:\\Program Files\\ShadowDPI",
  "installedUtc": "2026-11-15T00:00:00Z",
  "updatedUtc": "2026-11-15T00:00:00Z",
  "sourcePath": "C:\\Users\\you\\Downloads\\shadowdpi-0.5.0-x64",
  "payload": [
    "shadowdpi.exe",
    "WinDivert.dll",
    "WinDivert64.sys",
    "LICENSE",
    "NOTICE",
    "README.md",
    "install.ps1",
    "docs\\install.md",
    "tools\\scan-urls.txt"
  ],
  "service": {
    "enabled": true,
    "name": "ShadowDPI",
    "arguments": "-9",
    "startType": "auto",
    "running": true
  }
}
```

`installedUtc` is kept across upgrades. `updatedUtc`, `version`, `sourcePath`,
`payload`, and the service block are refreshed after every install or upgrade.
`reconfigure` keeps the recorded `sourcePath` and `payload` because it does not
replace files. `startType` is `auto`, `demand`, or `disabled`. `payload` lists
the files that belong to the installation; an upgrade removes files from the
previous payload that are absent from the new release and prunes folders that
become empty. An installation created by an older installer has no `payload`
list, so the first upgrade writes the list without removing anything.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | The action completed, the user cancelled, or the scan finished without an error. |
| `1` | A check failed, the service could not be changed, or the files could not be written. |
| `2` | The scan completed but no strategy restored every blocked probe. |

The scanner exits with `0` when the baseline already reached every URL, even if
there is no recommendation.

## Notes

- The installer does not modify DNS settings. The scanner only changes DNS
  behavior inside the candidate ShadowDPI processes.
- The installer does not create firewall rules. WinDivert loads its driver on
  first use.
- If an upgrade cannot replace `shadowdpi.exe`, a console instance is still
  running. Close it and run the action again.
- Service settings are stored in the Windows service database and mirrored in
  `install.json`. If the two disagree, `reconfigure` writes both again from the
  values you provide.
