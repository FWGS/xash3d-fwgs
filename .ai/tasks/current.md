G508 writable config/save round-trip probe is wired for the Dolphin-designated
storage route (`gcprobe:` RAM bank or real `sd:/xash3d`).

Next operator/runtime step:
```sh
DOLPHIN_NEWGAME=1 DOLPHIN_G508=1 DOLPHIN_G94=1 scripts/dolphin-boot-probe.sh
```

Expect guest markers:
- `G508 config round trip ready route=gcprobe` (or `route=sd`)
- optional `G94 round trip present` when `DOLPHIN_G94=1`

Physical SD/memory-card fault cases remain G38/G66/G71 hardware-only.
