# Presets

Ready-made command lines that ship with ShadowDPI. A preset is only a small
script that calls `shadowdpi.exe` with a fixed set of flags; the executable
itself stays generic.

| Folder | Contents |
| --- | --- |
| [turkiye](turkiye/README.md) | Recipes for Turkish providers, grouped into `genel` and `superonline` |

## Layout convention

Every region follows the same shape, with one folder per provider:

```text
presets/<region>/
  settings.cmd          shared executable path and DNS endpoints
  run.cmd               shared console runner
  install-service.cmd   shared service installer
  remove-service.cmd    shared service remover
  README.md             provider index and shared setup
  <provider>/
    settings.cmd        recipe table: SDP_COUNT, SDP_LABEL_n, SDP_ARGS_n
    README.md           recipes, usage, and notes for that provider
    console/            one launcher per recipe
    service/            install and remove wrappers
```

Rules that keep the scripts readable:

- Recipes are grouped by provider, and recipe numbers restart at 1 in each
  provider folder.
- Shared values live once in the region `settings.cmd`; provider files only add
  their own recipe table.
- A preset is applied first and overrides follow it, because later command line
  arguments win.
- DNS endpoints are variables, never hardcoded in the launchers.
- New scripts use `--fake-ttl`, not the `--set-ttl` compatibility alias.
- Scripts check for Administrator rights before they touch the service manager.
- Batch messages are ASCII, so `cmd.exe` code pages cannot garble them.

To add a provider, copy an existing provider folder, rename it, and edit its
`settings.cmd` and README. Add the folder to the region table.
