# Architecture

ShadowDPI separates privileged packet processing from everything a user
interacts with. This document describes the boundary used by the tray GUI.

## Processes

| Process | Privilege | Responsibility |
|---|---|---|
| `shadowdpi.exe` (service) | LocalSystem, elevated | Owns WinDivert handles, packet loop, trackers, service lifecycle, IPC server |
| `shadowdpi-gui.exe` | asInvoker, user session | Tray status, preset selection, emergency stop, journal view; never loads WinDivert |
| `install.ps1` | elevated | Copies files, registers the service, writes install metadata |

The service loads the WinDivert driver at runtime. The MSI installs only the
Win32 service; Windows Installer cannot declare a kernel driver service, so
driver loading stays in the service.

## IPC boundary

Transport is a Windows named pipe named `\\.\pipe\shadowdpi`. Messages are
length-prefixed and use a versioned binary protocol defined in
`include/shadowdpi/ipc.hpp`. The protocol version is carried in every message
and in the handshake, so a newer GUI can detect an older service cleanly.

The service builds the pipe DACL from `build_pipe_sddl()`. SYSTEM and
Administrators have full access; Authenticated Users can connect to read
status. No command is authorized by connection alone: the server impersonates
each caller and checks membership in the Administrators group or in the
optional `HKLM\Software\ShadowDPI\IpcAllowedSid` principal before allowing
`ApplyConfig`, `Stop`, `Start` or `RunScan`. `Handshake`, `GetStatus` and
`GetLog` are available to any authenticated caller. Without that registry
value, a standard user can observe but not change anything. The caller PID in
the handshake is informational and is never used as a credential.

Commands:

| Command | Status |
|---|---|
| `Handshake` | implemented, returns protocol version and caller PID |
| `GetStatus` | implemented, returns service state, exit code, checkpoint, PID |
| `ApplyConfig` | implemented, validates through `parse_cli()` and persists to `config.json`; takes effect after restart |
| `Stop` | implemented, requests shutdown through `ShutdownCoordinator` |
| `GetLog` | implemented, returns the recent service journal as JSON |
| `WriteBlacklist` | implemented, validates and saves `%ProgramData%\ShadowDPI\blacklist.txt` |
| `Start`, `RunScan` | reserved, return an explicit "not available in this phase" error |

The GUI requests a stop through the IPC `Stop` command. Starting a stopped
service is not available through IPC because a stopped service has no pipe;
use the Service Control Manager or `install.ps1` for that.

## Configuration ownership

Persisted settings live in `%ProgramData%\ShadowDPI\config.json`. The file has
the shape `{"arguments":"shadowdpi.exe\u0000-9\u0000..."}`: one JSON string
whose NUL-separated tokens are the same arguments the CLI accepts. The service
validates every write with `parse_cli()` and `finalize_config()` before saving,
so all validation, presets and `CliError` messages stay single-sourced. On
startup the service loads the file and uses it instead of the service command
line when it is present.

Writes use a temporary file and a rename, so an interrupted update cannot leave
a half-written config. The installer is responsible for setting the directory
ACL in a later phase; today the file is created with the default ACL of
`ProgramData`.

The saved blacklist lives at `%ProgramData%\ShadowDPI\blacklist.txt`. The
service validates it with `Blacklist::load()` before replacing it and adds
`--blacklist <path>` to the startup arguments on the next start. Both files are
service-owned; the GUI writes them through IPC and only reads the blacklist
file directly to populate its editor.

## Testing

`tests/ipc_tests.cpp` covers protocol encoding and malformed-message
rejection, argv encoding, SDDL construction, config store round-trips, and a
real pipe round-trip using the current user's SID. It requires no Administrator
rights because named pipes can be created by a normal user.
