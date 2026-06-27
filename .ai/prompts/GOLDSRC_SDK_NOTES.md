# Half-Life SDK and Content Layout (GoldSrc)

This port runs Xash3D-FWGS with the portable HL SDK. **Half-Life 1 game data is
never distributed with the port** — each player supplies their own legal `valve/`
tree on SD or disc. The engine must read that tree **as-is** (no prebake).

## Directory layout (retail `valve/`)

Typical runtime tree (names vary slightly by distribution):

```
valve/
  liblist.gam          # mod metadata
  delta.lst            # network delta tables (must match server/client)
  maps/*.bsp           # compiled maps (+ optional .res)
  models/*.mdl         # studio models
  sound/               # WAV voice and SFX
  sprites/*.spr
  gfx/                 # UI, fonts, VGUI-related art (.tga)
  media/               # StartupVids.txt + intro .avi (play natively on GC — target)
  *.wad                # halflife.wad, xeno.wad, decals.wad, ...
  events/*.sc          # weapon precache stubs
  resource/            # titles, cursors (mod-dependent)
```

Xash may also load `*.pk3` / ZIP archives and loose directories via search
paths initialized from `-game` and `gameinfo`/mod detection.

## HL SDK in this repository

This port builds **game logic** from the portable HL SDK fork, not the stock
Valve SDK tree. The **engine** is Xash3D-FWGS (`engine/`, `filesystem/`, `ref/`).

| Path | Role |
|------|------|
| `3rdparty/hlsdk-portable/` | Client/server game DLL sources (HUD, weapons, entities) |
| `stub/client/`, `stub/server/` | Static export shims for platforms without dynamic loading |
| `scripts/hlsdk-gamecube-probe.sh` | SDK presence/build probe |
| `scripts/hlsdk-gamecube-build.sh` | GameCube static library build |
| `scripts/hlsdk-gamecube-apply-patch.py` | Portable SDK patches for GC toolchain |
| `scripts/generate-hlsdk-gamecube-exports.py` | Export table generation |

### Official Valve SDK ([ValveSoftware/halflife](https://github.com/ValveSoftware/halflife))

Valve's public HL1 SDK is the canonical reference for **mod/game DLL** layout and
behavior. Use it to compare entity logic, HUD code, and shared headers — not as a
drop-in engine replacement.

| Valve SDK path | Typical use when debugging |
|----------------|----------------------------|
| `cl_dll/` | Client HUD, VGUI, client weapons/view code → maps to `3rdparty/hlsdk-portable/cl_dll/` |
| `dlls/` | Server game logic, entities, triggers |
| `common/`, `game_shared/`, `pm_shared/` | Shared structs, player movement, game constants |
| `public/` | Engine↔game DLL interface headers (`eiface.h`, `cdll_int.h`, etc.) |
| `network/` | Delta descriptions; retail `delta.lst` must stay consistent |
| `utils/` | Original compile tools (QC/SMD→MDL, VIS/CSG/RAD, spr makers) — host-side only |
| `engine/` (in Valve repo) | **Legacy reference only**; runtime engine here is Xash, not Valve's |

License: Valve SDK is free for non-commercial mod/source distribution; commercial
use requires Valve approval. Do not commit retail game assets or confuse SDK
license with redistribution of `Half-Life/valve` content (operator-owned copy).

Game logic bugs (HUD, weapons, entities) live in SDK + `engine/client/dll_int/`
and `engine/server/sv_game.c`. Asset load failures usually live in `filesystem/`,
`engine/common/mod_*.c`, or `engine/client/sound/`.

## GameCube content model

### What we ship

- Engine binary (`boot.dol`), optional homebrew ISO wrapper, docs, and license.
- **No** maps, models, sounds, WADs, textures, or intro video files.

### What the player provides

- A complete legal Half-Life 1 `valve/` directory (or mod) copied next to the
  engine on their boot medium, same layout as desktop Xash.

### What the engine must do (target)

- Parse every retail GoldSrc format at runtime: BSP, MDL, SPR, WAV, WAD3, PAK,
  TGA, CFG, AVI intros, etc., without a host-side conversion step.
- Fail visibly when files are missing; never silently drop features because a
  loader is unfinished.

## Dev disc staging (not player setup)

`scripts/build-gamecube-disc.py` assembles **local developer test ISOs** from an
operator-owned `Half-Life/valve` checkout:

- Copies content for Dolphin/CI smoke tests only; nothing here ships to players.
- Optional `gamecube-bootstrap.pk3` / `extras.pk3` repacking and `.avi`→`.gcvid`
  conversion are **bring-up shortcuts**. Track them as gaps to remove once native
  loaders and loose-file paths work on hardware.

Preflight in the Aider GUI warns when `Half-Life/valve` is missing locally;
that path is for developers and automation, not something end users build from repo.

## External references (analysis only)

- [ValveSoftware/halflife](https://github.com/ValveSoftware/halflife) — official HL1
  SDK (client/server DLLs, shared headers, utils). Compare behavior here; build
  from `3rdparty/hlsdk-portable/` in this repo.
- [HLLib](https://github.com/RavuAlHemio/hllib) — Ryan Gregg's library for reading
  PAK, WAD3, GCF/NCF, BSP (incl. embedded pak lumps), VPK; useful to validate
  archives without running the game.
- [bsp_tool](https://github.com/snake-biscuits/bsp_tool) — Python BSP lump inspection
  for GoldSrc/Valve BSP versions; use to compare map structure when `mod_bmodel`
  rejects a lump.
- [TWHL File Types](https://twhl.info/wiki/page/File_Types_and_Formats) — modder-facing
  format catalog and ship/source/temp conventions.

Do not copy proprietary SDK headers or retail assets from these tools into the repo.

## Automation expectations

- Goals touching maps, audio, video, or packaging must **not** add player-facing
  prebake or "convert your assets" workflows.
- Assume HL1 formats are in scope on GameCube unless a specific memory/IO limit is
  proven with evidence.
- Mark blockers with the failing path and loader (`FS_Open`, `Mod_LoadModel`,
  `AVI_OpenVideo`, etc.), not "unsupported on GameCube" or "user must preconvert".
