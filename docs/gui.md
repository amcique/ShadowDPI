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
- **Presets**: the nine documented presets with a short description. Selecting
  one validates it through the service config parser and saves it for the next
  service start.
- **Emergency stop**: requests service shutdown through the coordinator
- **Exit**: removes the tray icon and closes the GUI

Double-clicking the tray icon opens the dashboard. The status refreshes every
two seconds; the dashboard has a Refresh button for an immediate update.

The GUI runs with an `asInvoker` manifest. Standard users can read status and
the journal. Preset changes and emergency stop require Administrator membership
or the optional `IpcAllowedSid` principal; the service rejects those commands
otherwise and the GUI shows the rejection.

The GUI is built from the same CMake project. Use
`-DSHADOWDPI_BUILD_TESTS=OFF` to build only the executables.
