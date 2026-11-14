# Presets

Ready-made command lines that ship with ShadowDPI. A preset is only a small
script that calls `shadowdpi.exe` with a fixed set of flags; the executable
itself stays generic.

| Folder | Contents |
| --- | --- |
| [turkiye](turkiye/README.md) | Six recipes for DPI behavior seen with Turkish providers, with console and service launchers |

## Layout convention

Every preset folder follows the same shape:

```text
presets/<region>/
  settings.cmd      editable values: executable path, DNS endpoints, arguments
  console/          foreground launchers, one per recipe
  service/          install and remove scripts for the ShadowDPI service
  README.md         recipes, usage, and troubleshooting
```

Rules that keep the scripts readable:

- A preset is applied first and overrides follow it, because later command line
  arguments win.
- DNS endpoints are variables, never hardcoded in the launchers.
- New scripts use `--fake-ttl`, not the `--set-ttl` compatibility alias.
- Scripts check for Administrator rights before they touch the service manager.
- Batch messages are ASCII, so `cmd.exe` code pages cannot garble them.

To add a region, copy `turkiye`, rename the folder, and edit `settings.cmd` and
its README. Add the new folder to the table above.
