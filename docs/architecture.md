# Architecture

ShadowDPI separates privileged packet processing from everything a user
interacts with. This document describes the boundary introduced for the
future GUI. The GUI itself is not implemented yet.

## Processes

| Process | Privilege | Responsibility |
|---|---|---|
| `shadowdpi.exe` (service) | LocalSystem, elevated | Owns WinDivert handles, packet loop, trackers, service lifecycle, IPC server |
| `shadowdpi-gui.exe` (planned) | asInvoker, user session | Status, presets, scanner wizard, log view; never loads WinDivert |
| `install.ps1` | elevated | Copies files, registers the service, writes install metadata |

The service already installs the WinDivert driver at runtime. A future MSI
installs only the Win32 service; Windows Installer cannot declare a kernel
driver service, so driver loading stays in the service.

## IPC boundary

Transport is a Windows named pipe named `\\.\pipe\shadowdpi`. Messages are
length-prefixed and use a versioned binary protocol defined in
`include/shadowdpi/ipc.hpp`. The protocol version is carried in every message
and in the handshake, so a newer GUI can detect an older service cleanly.

The service builds the pipe DACL from `build_pipe_sddl()`. By default only
SYSTEM and Administrators are granted access. An optional registry value
`HKLM\Software\ShadowDPI\IpcAllowedSid` can add one user SID, allowing an
installer to delegate routine control to a non-elevated user. Everyone else is
denied implicitly. The caller PID in the handshake is informational and is
never used as a credential.

Commands:

| Command | Phase 0 status |
|---|---|
| `Handshake` | implemented, returns protocol version and caller PID |
| `GetStatus` | implemented, returns service state, exit code, checkpoint, PID |
| `ApplyConfig` | implemented, validates through `parse_cli()` and persists to `config.json`; takes effect after restart |
| `Stop` | implemented, requests shutdown through `ShutdownCoordinator` |
| `Start`, `RunScan`, `GetLog` | reserved, return an explicit "not available in this phase" error |

The GUI is expected to start and stop the service through the Service Control
Manager, not through IPC, because a stopped service has no pipe.

## Configuration ownership

Persisted settings live in `%ProgramData%\ShadowDPI\config.json`. The file
holds an `arguments` array containing the same tokens the CLI accepts. The
service validates every write with `parse_cli()` and `finalize_config()` before
saving, so all validation, presets and `CliError` messages stay single-sourced.

Writes use a temporary file and a rename, so an interrupted update cannot leave
a half-written config. The installer is responsible for setting the directory
ACL in a later phase; today the file is created with the default ACL of
`ProgramData`.

## Testing

`tests/ipc_tests.cpp` covers protocol encoding and malformed-message
rejection, argv encoding, SDDL construction, config store round-trips, and a
real pipe round-trip using the current user's SID. It requires no Administrator
rights because named pipes can be created by a normal user.
