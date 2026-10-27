# GoldSrc / Half-Life 1 Content Formats

Half-Life 1 shipped in 1998 on PC hardware weaker and older than the GameCube
(2001). GoldSrc asset formats are not inherently beyond this port. When intro
video, maps, models, or sounds fail on GameCube, treat it as a loader, staging,
memory-budget, or path bug — not as proof the format is impossible on GC.

Reference summaries: [TWHL File Types and Formats](https://twhl.info/wiki/page/File_Types_and_Formats).
Format inspection tools (do not vendor into the repo): [ValveSoftware/halflife](https://github.com/ValveSoftware/halflife)
(SDK layout and game-DLL reference), [HLLib](https://github.com/RavuAlHemio/hllib)
(PAK/WAD/GCF/BSP/VPK extraction), [bsp_tool](https://github.com/snake-biscuits/bsp_tool)
(GoldSrc/Valve BSP lump analysis).

## Port policy

### Legal: user-provided content only

- **Never ship Half-Life 1 retail assets** with this port (ISO, DOL package, repo,
  or release archive). Maps, models, sounds, WADs, and `media/*.avi` remain the
  player's legally obtained copy.
- Releases ship **engine + homebrew metadata only**. Document that the player
  copies their own `valve/` tree to SD/disc beside `boot.dol`.
- Automation must not commit, stage into git, or redistribute proprietary game
  files. Local `Half-Life/valve` is for developer testing only.

### Technical: native runtime format support (target)

- The GameCube port must **read GoldSrc formats as the user provides them** —
  no required prebake, conversion toolchain, or repack step for the player.
- Retail `.bsp`, `.mdl`, `.spr`, `.wav`, `.wad`, `.pak`, `.tga`, `.cfg`,
  `media/*.avi`, and the rest of the HL1 tree should load through Xash loaders on
  GC the same way they do on desktop, subject only to memory/IO engineering.
- When a format fails on GameCube, **implement or fix the loader** — do not
  treat `-nointro`, stripped assets, or permanent desktop-only paths as the
  product answer.
- Do not assume HL1 formats are too old or too heavy for GameCube; they predate
  the hardware. Blockers are missing code, endian bugs, or budget — not format age.

### Dev/CI staging

Some scripts today shortcut bring-up:

- `scripts/build-gamecube-disc.py` may pack bootstrap PK3s for emulator smoke
  tests, but it must not preconvert retail movie assets.
- GameCube startup video support lives in `engine/client/avi/avi_gc.c` and should
  read the user's original `media/*.avi` files at runtime.

Treat any attempted `.avi` prebake or `.gcvid` dependency as a regression. The
goal is direct `.avi` (and every other retail format) playback/load at runtime.

- Source-engine formats (`.vmt`, `.vtf`, `.vmf`, Source `.vpk`) are out of scope
  unless explicitly bridged; GoldSrc HL1 retail does not depend on them.

## Ship vs source vs temp (modding flags)

| Flag | Meaning for this port |
|------|------------------------|
| Source / backup | Editor or toolchain input; not required on disc if compiled output exists |
| Temp / build | Leak files (`.lin`, `.prt`, `.pnt`), compile logs — never ship |
| Ship | Runtime files the engine reads from `valve/` or staged disc image |

## Runtime formats (GoldSrc HL1)

| Ext | Name | Relevance | Engine / staging notes |
|-----|------|-----------|------------------------|
| `.bsp` | Binary Space Partition | Maps | `engine/common/mod_bmodel.c`; GoldSrc BSP ≠ Source BSP. Inspect lumps with bsp_tool. |
| `.mdl` | Studio model | Players, NPCs, props | `engine/common/mod_studio.c`, `ref/gx/r_studio.c`. |
| `.spr` | Sprite | HUD, effects, map sprites | `engine/common/mod_sprite.c`, `ref/gx/r_sprite.c`. |
| `.wav` | Wave audio | SFX, voice | `engine/client/sound/s_load.c`; 8-bit 8 kHz minimum, up to 16-bit 22 kHz; loop cue points in file. |
| `.mp3` | MPEG Layer 3 | `media/` CD replacement (Steam) | May substitute where WAV accepted; not primary on original HL1 retail layout. |
| `.wad` | WAD3 texture package | Map/world textures | `filesystem/wad.c` (WAD2/WAD3); retail `halflife.wad`, `xeno.wad`, `decals.wad`. |
| `.pak` | PAK archive | Legacy HL packaging | `filesystem/pak.c`; still supported; Xash also uses ZIP/PK3 search paths. |
| `.tga` | Targa | Skybox, detail, UI | Loaded via renderer/image paths; common in `valve/gfx`. |
| `.bmp` | Bitmap | Source art for WAD/models | Indexed 256-color; compile-time source, not always shipped loose. |
| `.cfg` | Config / exec scripts | `config.cfg`, map configs, `gamecube.cfg` | Plain text cvar/command lists. |
| `.res` | Map resource list | Fast-download precache list | Per-map beside `.bsp` in `maps/`; unrelated to Steam UI `.res` keyvalues. |
| `.txt` | Plain text data | `titles.txt`, `sentences.txt`, `hud.txt`, `StartupVids.txt` | `DEFAULT_VIDEOLIST_PATH` = `media/StartupVids.txt`. |
| `.lst` | Lists | Network (`delta.lst`) or shell keybind lists | Server/client must match network lists. |
| `.gam` | `liblist.gam` | Mod metadata | Describes mod/game properties. |
| `.scr` | Settings layout | `settings.scr`, `user.scr` | UI advanced options layout. |
| `.avi` | AVI video | Intro cinematics in `media/` | Decode retail AVIs on GC at runtime through `engine/client/avi/avi_gc.c` (HL1 uses AVI containers, commonly Cinepak). FFmpeg remains a desktop/optional backend. |
| `.gcvid` | GameCube intro stream | Deprecated generated payload | Do not add new dependencies on this format. Prefer native `.avi` playback from user-owned assets. |

## Movies and intro playback

GoldSrc startup reads `media/StartupVids.txt`, then plays listed cinematics
(`media/valve.avi`, `media/sierra.avi`, etc.) through `SCR_CheckStartupVids()`
in `engine/client/cl_video.c`.

**Product requirement:** a player who copies a legal retail `valve/` folder should
see intros and all media without converting files. Implement on-GameCube AVI/movie
decoding (or an in-engine subset matching retail codecs) rather than documenting
intros as permanently disabled or requiring a host-side prebake step.

**Current requirement:** GameCube must open the original playlist entries
directly. Automation should preserve this path and fix decoder/runtime bugs rather
than introducing host-side conversion.

## Mapping / compile toolchain (usually not on disc)

`.map`, `.rmf`, `.jmf`, `.fgd`, `.rad`, `.qc`, `.smd` — editor or compile
inputs. The port consumes compiled `.bsp` (+ `.res` if used), not Hammer sources.

## Packages and filesystem in this repo

- Search path setup: `filesystem/searchpath.c`, `engine/common/filesystem_engine.c`.
- WAD/PK3/ZIP/PAK: `filesystem/wad.c`, `filesystem/zip.c`, `filesystem/pak.c` —
  must consume **user-supplied** retail archives unchanged.
- `scripts/build-gamecube-disc.py` copies a developer's local `Half-Life/valve`
  into test ISOs; optional PK3/bootstrap packing is a **CI/dev convenience**, not
  a player setup step. Do not preconvert retail movie assets.
- Campaign map audit: `scripts/gamecube-campaign-audit.sh` over legal local
  `Half-Life/valve/maps/*.bsp`.

## When content fails

1. Confirm the **user's** `valve/` (or `-game` mod tree) is present on the boot
   medium — the port does not bundle it.
2. Read Dolphin/host logs for `Adding WAD`, `Adding ZIP`, `Couldn't load`, model/sound errors.
3. Distinguish missing file vs parser/endian bug vs memory cap vs **avoidable
   prebake workaround** still in code.
4. Fix loaders in-engine; do not add new user-facing conversion steps.
5. Use HLLib/bsp_tool offline to validate archives or BSP lumps; fix engine code here.
