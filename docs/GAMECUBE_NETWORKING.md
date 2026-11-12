# GameCube networking (LAN + internet)

Build with `XASH_GAMECUBE_LINK_BBA=1` (default in `scripts/build-gamecube.sh`) so libogc2's
network stack is linked. libogc2 drives the retail Broadband Adapter and the modern
SP1 Ethernet adapters (eth2sp1 W5500/ENC28J60 class); classic libogc supports the BBA only.

| Need | How |
| --- | --- |
| DHCP (default) | Plug in, start a multiplayer game; `GC_NetEnsureReady()` brings the link up lazily. |
| Static IP | `-netstatic [-gcnetip a.b.c.d -gcnetmask m -gcnetgw g]` (default 10.0.0.2/24, gw 10.0.0.1, validated by `net_config.h`). |
| LAN browse | Uses UDP broadcast (`255.255.255.255`) on the engine's normal ports. |
| Internet | Hostnames (master servers, `connect host`) resolve through `net_dns.h` (UDP DNS A lookup, 1.5 s timeout per server). Servers tried in order: `-gcdns a.b.c.d`, the DHCP/static gateway, 1.1.1.1, 8.8.8.8. Successful lookups are cached (8 entries). |
| Disable | `-nonet` / `-noip`. |

Behind NAT, a client can join any public server and play. To host over the internet,
forward the server UDP port (default 27015) to the console's IP.

## Verification status

Host unit tests (`python3 -m unittest tests.test_gamecube_net_dns`) cover DNS query
building and response parsing. The target build and a real BBA / eth2sp1 / Dolphin
BuiltIn-BBA run have not been done yet; they need devkitPro on Tim's machine.
