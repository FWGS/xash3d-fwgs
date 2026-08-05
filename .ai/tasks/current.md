Swiss/libogc2 stack modernization is in progress for the GameCube port.

Preferred build stack:
- Extrems **libogc2** under `$DEVKITPRO/libogc2/gamecube`
- **libdvm** as the FAT provider (`fatInitDefault` API unchanged)
- Swiss as the hardware loader path

Override: `XASH_GAMECUBE_OGC_STACK=libogc2|libogc|auto`

Verify stack:
```sh
python3 scripts/waifulib/gamecube_ogc_stack.py
scripts/build-gamecube.sh
```

Outstanding runtime gates (unchanged): G508 config round-trip Dolphin
evidence and G509 changelevel soak on a machine with the toolchain + Dolphin.
