# Multi-game support on GameCube

Xash3D-GC stays **one repository** for the engine, GX, disc tooling, and HL1
work. Each playable GoldSrc title is a **separate statically linked DOL** with
a unique name. Shared media under `xash3d/` can hold multiple asset trees.

## Release DOL names

| Title | Linked gamedir (`--gamedir` / `HLSDK_GAMECUBE_GAMEDIR`) | DOL |
|-------|----------------------------------------------------------|-----|
| Half-Life 1 | `valve` | `OUT/bin/halflife1.dol` (alias `OUT/bin/boot.dol`) |
| Day of Defeat | `dod` | `OUT/bin/dodgc.dol` |

Default builds remain HL1. Existing Swiss/ISO docs that mention `boot.dol` keep
working for Half-Life.

```bash
# Half-Life 1 (default)
scripts/build-gamecube.sh
# or
scripts/build-gamecube.sh --game-profile=halflife1

# Day of Defeat (requires DoD HLSDK archives — see research gate below)
HLSDK_GAMECUBE_GAMEDIR=dod scripts/hlsdk-gamecube-build.sh
HLSDK_GAMECUBE_GAMEDIR=dod scripts/build-gamecube.sh
```

## Disc / SD layout

```
{sd|gcdisc}:/xash3d/valve/   # HL1 retail (and DoD basedir fallback)
{sd|gcdisc}:/xash3d/dod/     # DoD retail when staged
apps/xash3d-gc/boot.dol      # primary boot (HL1 by default)
apps/xash3d-gc/halflife1.dol # named HL1 release (when copied)
apps/xash3d-gc/dodgc.dol     # named DoD release (when built)
```

`scripts/build-gamecube-disc.py`:

- `--gamedir` sets the primary `/xash3d/<name>` graft (default `valve`)
- `--extra-data dod=Half-Life/dod` adds another graft
- sibling folders next to `--data` (e.g. `dod` beside `valve`) are auto-staged
  unless `--no-auto-extra-gamedirs` is set

## Runtime detection

At boot the platform layer scans `xash3d/` for game folders (`liblist.gam` /
`gfx.wad`), logs `linked gamedir=… detected=…`, and still requires **strict
HL1 checks** when the linked title is `valve`. The DOL’s baked `-game`
(`XASH_GAMEDIR`) selects which tree is playable; there is no Custom Game /
restart-to-switch-title path on GameCube yet.

## Day of Defeat research gate

Do **not** claim `dodgc.dol` gameplay until all of the following are true:

1. **Assets** — retail/Steam `dod/` tree is available beside `valve/` (DoD
   `liblist.gam` typically falls back to `valve`).
2. **Game code** — Steam Day of Defeat is listed as **vgui2-blocked** in
   [`Documentation/not-supported-mod-list-and-reasons-why.md`](../Documentation/not-supported-mod-list-and-reasons-why.md).
   Stock Xash3D does not support that client. Need either:
   - an open reverse-engineered DoD client/server suitable for static GC link, or
   - an older non-vgui2 DoD client, or
   - an explicit vgui2 porting effort (out of scope until chosen).
3. **HLSDK GC archives** — `OUT/hlsdk-gamecube/dod/dlls/libhl_gamecube_ppc.a`
   and matching client archive built and linked into `dodgc.dol`.

Until that gate passes, the engine may **detect and stage** `dod/` next to
`valve/`, but only the HL1-linked DOL (`halflife1.dol` / `boot.dol`) is the
supported play path.

Launcher: `scripts/play-day-of-defeat-gamecube.sh` requires `dodgc.dol` and
`dod/` assets; it no longer silently builds an HL1 disc. For Half-Life use
`scripts/play-dolphin.sh`.
