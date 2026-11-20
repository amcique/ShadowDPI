# Tray GUI

`shadowdpi-gui.exe` is an optional unprivileged front end for the Windows
service. It never loads WinDivert and never needs Administrator rights: all
privileged work goes through the named-pipe IPC described in
[architecture.md](architecture.md).

Start it from an elevated or normal prompt:

```powershell
.\shadowdpi-gui.exe
```

The tray icon shows the service state and active preset. Right-click for:

- **Open dashboard**: state, preset, process ID and the recent service journal
- **Find a working strategy**: scanner wizard described below
- **Edit blacklist**: edits the service-owned blacklist file
- **Edit DNS resolver**: sets IPv4 and IPv6 resolver addresses and ports
- **Advanced arguments**: sends raw CLI arguments through the config validator
- **Presets**: the nine documented presets with a short description. Selecting
  one validates it through the service config parser and saves it for the next
  service start.
- **Emergency stop**: requests service shutdown through the coordinator
- **Exit**: removes the tray icon and closes the GUI

Double-clicking the tray icon opens the dashboard. The status refreshes every
two seconds; the dashboard has a Refresh button for an immediate update.

## Scanner wizard

The wizard collects URLs, a URL file, scan level, protocol mode, IP version,
timeout and an optional DNS address. It launches `install.ps1 -Action scan`
through an elevated PowerShell, so Windows shows one consent prompt for the
scan. The scanner stops the service for its baseline and restarts it when
finished. The wizard follows progress from `-ProgressFile`, then reads the
`-Json` summary and shows the recommended strategy. **Apply recommended
strategy** sends that strategy to the service through `ApplyConfig`.

The DNS resolver choice is always `system` unless an explicit DNS address is
entered, so the elevated scan never waits for an interactive prompt.

## Editors

The blacklist editor writes `%ProgramData%\ShadowDPI\blacklist.txt` through the
service. The service validates the content with `Blacklist::load()` before
replacing the file. The saved file is added to the service arguments on the
next start.

The DNS editor builds `--dns-addr` / `--dnsv6-addr` arguments and sends them
through `ApplyConfig`. The advanced panel does the same with raw argument text.
Every value is validated by the service's CLI parser, and any `CliError` is
shown in the dialog.

The GUI runs with an `asInvoker` manifest. Standard users can read status and
the journal. Preset changes, blacklist edits and emergency stop require
Administrator membership or the optional `IpcAllowedSid` principal; the service
rejects those commands otherwise and the GUI shows the rejection.

The GUI is built from the same CMake project. Use
`-DSHADOWDPI_BUILD_TESTS=OFF` to build only the executables.
