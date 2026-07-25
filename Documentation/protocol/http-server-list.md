# Static HTTP server list

Replacement for the UDP `S2M_SCAN_REQUEST` / `M2A_SERVERSLIST` exchange
described in [02-connectionless.md](02-connectionless.md). Read-only HTTP,
no NAT punching, no filtering or pagination in the request.

## URL

`xashcomm.lst` carries a base URL per source:

```
masterstatic http://master.example.org/server-list
```

The engine appends `/v1/servers/<gamedir>` and `GET`s the result. For
`gamedir = valve`:

```
GET http://master.example.org/server-list/v1/servers/valve
```

Trailing slashes on the base URL are stripped. Multiple `masterstatic`
lines are allowed; results are merged.

The request is bare: no body, no auth, no cookies, no compressed encodings.
Only the standard `User-Agent` is set. `POST` / `PUT` / `DELETE` are not
used; servers register out of band.

## Response

UTF-8 text, parsed line by line. Each line is tokenized with
`COM_ParseFileSafe` (whitespace separates, `//` and `#` start line
comments, `"..."` quotes a token). Blank lines and comment-only lines
are ignored. One directive per line:

* `ip <address>` — Xash3D server (protocol 49).
* `gs <address>` — GoldSrc server (protocol 48).

`<address>` is parsed by `NET_StringToAdr` (`1.2.3.4:27015`,
`[2001:db8::1]:27015`, hostnames). Port defaults to `27015`. Lines
starting with an unknown directive are skipped entirely, so new keywords
with any number of operands can be added without breaking older clients.

`Content-Type` is not inspected, `text/plain; charset=utf-8` expected.

### Example

```
# diffusion servers
ip 192.0.2.10:27015
ip [2001:db8::1]:27015
gs 198.51.100.5:27015
```

A file with zero records is valid and represents an empty list.

### Versioning

The `/v1/` segment is fixed in this revision. A future protocol revision
adds a sibling `/v2/...` resource without breaking older clients.

## Client behaviour

Each `ip` / `gs` record from the response triggers a probe to the listed
address, identical to a server discovered through the UDP master. DNS
errors, non-200 responses, and malformed bodies are reported to the
console.

## Server behaviour

Any static HTTP server works. A typical setup is a periodic job that
probes a set of known addresses and writes `v1/servers/<gamedir>`.
