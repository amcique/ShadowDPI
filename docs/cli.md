# Command-line reference

ShadowDPI runs as an elevated console program. Start it from an Administrator
terminal, or let the embedded manifest trigger a UAC prompt when you
double-click the executable.

```
shadowdpi.exe [OPTION...]
```

Running with no arguments is the same as `-9`.

## Presets

Presets are shorthand for groups of options. They are applied in order, so
later arguments still override them.

| Preset | What it turns on | Typical use |
| --- | --- | --- |
| `-1` | Passive DPI blocking, Host tricks, HTTP split 2, TLS split 2, keep-alive handling | Simple HTTP/HTTPS DPI |
| `-2` | Same as `-1`, but TLS split 40 | DPI that reassembles small TLS splits |
| `-3` | Passive DPI blocking, Host tricks, TLS split 40 | No HTTP keep-alive handling |
| `-4` | Passive DPI blocking and Host tricks only | DPI that only inspects headers |
| `-5` | HTTP/TLS split 2, native split, reverse order, automatic fake TTL, max payload 1200 | The usual starting point when `-9` is too aggressive |
| `-6` | HTTP/TLS split 2, native split, reverse order, wrong-SEQ fake packets, max payload 1200 | DPI that ignores fake TTL |
| `-7` | HTTP/TLS split 2, native split, reverse order, bad-checksum fake packets, max payload 1200 | DPI that validates TTL |
| `-8` | HTTP/TLS split 2, native split, reverse order, wrong-SEQ and bad-checksum fake packets, max payload 1200 | Stubborn DPI |
| `-9` | `-8` plus QUIC/HTTP3 blocking | Default; best first try |

`-9` blocks QUIC on purpose. Browsers then fall back to TCP, where the
fragmentation and fake-packet tricks apply. If an application depends on QUIC
and cannot fall back, use `-5` instead.

## Options

### General

| Option | Meaning |
| --- | --- |
| `-h`, `--help` | Print the help text of this exact build and exit. |
| `-V`, `--version` | Print the version and exit. |
| `-v`, `--verbose` | Enable verbose diagnostics. |
| `--preset <1-9>` | Apply a preset. The short forms `-1` to `-9` are equivalent. |
| `--debug-exit` | Open the WinDivert filters, report success, and exit. Useful for diagnostics. |
| `--ready-file <path>` | Write a readiness file after the filters are open, then keep running. The file contains the version and process ID. Used by `install.ps1 -Action scan`; the caller removes stale files before starting. |

### Passive DPI and QUIC

| Option | Meaning |
| --- | --- |
| `-p`, `--block-passive` | Drop injected HTTP redirects and TCP resets that try to answer before the real server. |
| `-q`, `--block-quic` | Drop QUIC/HTTP3 Initial packets so applications use TCP. |

### HTTP

| Option | Meaning |
| --- | --- |
| `-f`, `--http-fragment <size>` | Enable HTTP fragmentation with the given split size. |
| `-k`, `--http-keepalive-fragment <n>` | Fragment persistent HTTP with split `n`; also enables native splitting and persistent-request parsing. |
| `-n`, `--http-keepalive-nowait` | Enable persistent-request parsing and native splitting immediately. ShadowDPI never waits for the first ACK; the flag is retained for compatibility. |
| `-r`, `--http-host-replace` | Replace the `Host` header name with `hoSt`. |
| `-s`, `--http-host-no-space` | Remove the space after `Host:`. |
| `-a`, `--http-extra-space` | Add a space between the method and the URI; implies `-s`. |
| `-m`, `--http-host-mixed-case` | Alternate the case of the `Host` value. |
| `-w`, `--http-all-ports` | Parse HTTP on every processed port, not only the usual HTTP ports. |

### HTTPS

| Option | Meaning |
| --- | --- |
| `-e`, `--https-fragment <size>` | Split the TLS ClientHello at the given size. |
| `--frag-by-sni` | Split immediately before the SNI extension. Implies native splitting and a TLS split size of 2 when none was set. |

### Fragmentation

| Option | Meaning |
| --- | --- |
| `--native-frag` | Split packets into smaller TCP segments. Without any `-f`/`-e` size, defaults to split size 2 for both HTTP and HTTPS. |
| `--reverse-frag` | Send the split segments in reverse order. Implies `--native-frag` and its default split size. |
| `--max-payload [size]` | Limit processed payloads to those smaller than `size` bytes; GET/POST requests and TLS handshakes still pass. Without a value the limit is 1200; presets 5-9 set 1200. |

### Fake request mode

Fake packets are sent with a TTL, checksum, or sequence number that makes the
DPI accept them while the real destination discards them.

| Option | Meaning |
| --- | --- |
| `--fake-ttl <ttl>` | Enable fake packets with a fixed TTL (1-255). Zero is rejected: a TTL of zero would send nothing. |
| `--auto-ttl [first-second-max]` | Derive the fake TTL from observed hop counts. Without a value the range is `1-4-10`; a single value sets both bounds to that value. Implies `--min-ttl 3` when no minimum was given. |
| `--min-ttl <hops>` | Suppress fake packets when the observed hop distance is too small. Requires a TTL option. |
| `--fake-wrong-checksum` | Give fake packets a deliberately invalid checksum. |
| `--fake-wrong-seq` | Give fake packets an old sequence/acknowledgement number. |
| `--fake-hex <hex>` | Add a custom fake payload from hexadecimal bytes. Enables fake mode. |
| `--fake-sni <domain>` | Add a generated ClientHello for `domain` (LDH labels, optional trailing dot). Enables fake mode. |
| `--fake-gen <count>` | Add `count` random fake payloads. Enables fake mode. |
| `--fake-resend <count>` | Send each fake payload `count` times (default 1; 0 disables the effect). |

At most 30 custom or generated fake payloads can be configured. Custom payload
options enable fake mode and need a neutralizer: `--fake-ttl`, `--auto-ttl`,
`--fake-wrong-seq`, or `--fake-wrong-checksum`. Configurations without one are
rejected. `--auto-ttl` emits nothing until a SYN/ACK is observed; a made-up TTL
could deliver the decoy to the server.

### Host selection

| Option | Meaning |
| --- | --- |
| `--blacklist <file>` | Apply the host-aware tricks only to hosts in the file. May be supplied more than once. |
| `--allow-no-sni` | Apply HTTPS tricks when the ClientHello has no SNI. |

Blacklist files contain one domain per line. Blank lines and lines starting
with `#` or `;` are ignored. A leading dot is optional. A match on a parent
domain also covers its subdomains.

The blacklist gates the HTTP `Host` and TLS SNI paths. The SYN/ACK window-size
step used by presets 1-4 runs before any host name is available, so it still
applies to every connection on the handled ports.

### Networking

| Option | Meaning |
| --- | --- |
| `--port <port>` | Process an additional TCP port (1-65535). May be repeated. |
| `--ip-id <id>` | Add an IP identification value to treat as passive-DPI traffic (0-65535). May be repeated. |
| `--dns-addr <ipv4>` | Redirect IPv4 DNS requests to this resolver. May be supplied once. |
| `--dns-port <port>` | Port for the redirected IPv4 DNS requests (53 by default). |
| `--dnsv6-addr <ipv6>` | Redirect IPv6 DNS requests to this resolver. May be supplied once. |
| `--dnsv6-port <port>` | Port for the redirected IPv6 DNS requests (53 by default). |

`--dns-port` requires `--dns-addr`, and `--dnsv6-port` requires
`--dnsv6-addr`.

## Compatibility aliases

The long names introduced by earlier DPI tools are accepted so existing
scripts keep working:

| Alias | Equivalent |
| --- | --- |
| `--passive-dpi` | `--block-passive` |
| `--set-ttl` | `--fake-ttl` |
| `--wrong-chksum` | `--fake-wrong-checksum` |
| `--wrong-seq` | `--fake-wrong-seq` |
| `--fake-from-hex` | `--fake-hex` |
| `--fake-with-sni` | `--fake-sni` |
| `--dns-verb` | `--verbose` |

## Examples

Run the default mode:

```powershell
.\shadowdpi.exe
```

Try the less aggressive fake-TTL preset:

```powershell
.\shadowdpi.exe -5
```

Apply the host-aware tricks only to a list of blocked domains:

```powershell
.\shadowdpi.exe -5 --blacklist C:\Tools\ShadowDPI\blocked.txt
```

Redirect DNS when the provider poisons answers:

```powershell
.\shadowdpi.exe -5 --dns-addr 77.88.8.8 --dns-port 1253
```

To find a working combination automatically, run
[the strategy scanner](scan.md) against your blocked sites.

Diagnose filter setup without leaving the program running:

```powershell
.\shadowdpi.exe -9 --verbose --debug-exit
```

Signal readiness to another program after the filters are open:

```powershell
.\shadowdpi.exe -5 --ready-file C:\Temp\shadowdpi.ready
```

## Validation rules

- Fragment sizes are 16-bit values; repeated fragment options keep the last
  value.
- `--native-frag`, `--reverse-frag`, and `-n` default to HTTP and HTTPS split
  size 2 when no `-f`/`-e` size was given; explicit sizes are never overwritten.
- `--reverse-frag` and `--frag-by-sni` enable native splitting automatically.
- Fake mode requires a neutralizer; `--min-ttl` alone is not one.
- `--http-extra-space` also removes the space after `Host:`.
- Configuration errors are printed to standard error and the process exits
  with a non-zero status.
- Pass `--` to stop option parsing; positional arguments are then rejected.

## Exit behavior

In console mode, ShadowDPI runs until you close the window or press Ctrl+C.
When started by the Service Control Manager it registers as the `ShadowDPI`
service and shuts down on a stop request. See the service section of the
[README](../README.md#windows-service) for the `sc` commands.
