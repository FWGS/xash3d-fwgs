# GameCube Networking Notes

Goal:

- Single-player must work offline (loopback). Missing BBA / eth2sp1 must not crash.
- Multiplayer / online play uses real UDP over the libogc/libogc2 network stack,
  which drives the official Broadband Adapter **and** Swiss-emulated ETH2SP1
  (W5500/ENC28J60 on Serial Port 1).

## Stack

| Layer | Implementation |
|-------|----------------|
| Hardware / Swiss | Real BBA, or ETH2SP1 via Swiss BBA emulation (v0.6r2018+) |
| Guest driver | libogc / libogc2 `if_config` + `net_*` sockets |
| Xash glue | `engine/platform/gamecube/net_gamecube.{c,h}` |
| Engine | `net_ws.c` (UDP / netchan); IPv6 disabled on GC |

## Boot policy

- Default: `NET_Config(false,false)` — loopback only, no DHCP wait.
- `-net` at boot: early `GC_NetEnsureReady()` (soft-fail).
- Multiplayer (`maxplayers > 1`, `connect`, listen): lazy bring-up then UDP bind.
- `-nonet` / `-noip`: skip bring-up entirely.
- HTTP / TLS / masters stay disabled on GameCube for now.

## Verify

- Boot without network hardware (offline SP).
- Dolphin BBA (TAP / XLink Kai / Built-in) + `-net` → OSReport `net ready ip=…`.
- Hardware: Swiss + ETH2SP1 or BBA → same marker, then `maxplayers 8` / `map` / LAN `connect`.
- Client/server spawn, disconnect/shutdown.
- Changelevel and save/load once those goals are active.
