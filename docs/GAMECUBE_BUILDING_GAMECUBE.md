# Building for GameCube (Swiss / libogc2)

This document describes the current Waf + Swiss-oriented build path for
xash3d-gc. Prefer Extrems **libogc2** and **libdvm** so the DOL matches the
Swiss loader / accessory stack used on real hardware.

## Prerequisites

- Linux or macOS (Windows via WSL2)
- Python 3
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with **devkitPPC**
- **libogc2** (preferred) or classic **libogc**
- Legal Half-Life assets for disc/ISO packaging (optional for bare DOL tests)

## Install the Swiss stack

```bash
# Base toolchain
sudo (dkp-)pacman -S --needed devkitPPC gamecube-tools

# Extrems libogc2 (Swiss interoperability)
sudo (dkp-)pacman -S libogc2 libogc2-docs

# When asked for a libogc2-libfat provider, prefer libogc2-libdvm.
# See: https://github.com/extremscorner/libogc2
```

Expected layout after install:

| Role | Path |
|------|------|
| Headers | `$DEVKITPRO/libogc2/gamecube/include` |
| Libraries | `$DEVKITPRO/libogc2/gamecube/lib` (`libogc.a`, `libasnd.a`, `libiso9660.a`, `libfat.a` / `libdvm.a`) |
| Make rules | `$DEVKITPRO/libogc2/gamecube_rules` |

Classic fallback (not preferred for Swiss):

| Role | Path |
|------|------|
| Headers | `$DEVKITPRO/libogc/include` |
| Libraries | `$DEVKITPRO/libogc/lib/cube` |
| Specs | `$DEVKITPRO/libogc/share/ogc.specs` |

Environment:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH
# auto (default) | libogc2 | libogc
export XASH_GAMECUBE_OGC_STACK=auto
```

Verify resolution without a full build:

```bash
python3 scripts/waifulib/gamecube_ogc_stack.py
```

## Build (Waf — current path)

From the repo root:

```bash
# HLSDK static archives first (required for Flipper production builds)
scripts/hlsdk-gamecube-build.sh

# Engine + DOL (+ optional ISO if Half-Life/valve exists)
scripts/build-gamecube.sh
```

Artifacts:

- `OUT/bin/xash` — PowerPC ELF
- `OUT/bin/boot.dol` — GameCube DOL for Swiss / Dolphin
- `OUT/bin/gamecube-handoff.txt` — stack + size metadata (`loader=swiss`, `ogc_stack=…`)
- `OUT/xash3d-gc.iso` — disc image when assets are present

Configure knobs used by the script:

```bash
./waf configure --gamecube \
  -T release \
  --disable-gl --disable-soft --enable-gx \
  --low-memory-mode=2 \
  --disable-werror
./waf build
```

## Running on hardware (Swiss)

1. Copy `OUT/bin/boot.dol` to SD/USB media Swiss can browse.
2. Place Half-Life data on a libdvm volume Swiss/libogc2 can mount:
   - `sd:/xash3d/valve/` — SD2SP2 (Serial Port 2)
   - `carda:/xash3d/valve/` or `cardb:/xash3d/valve/` — SD Gecko
3. Launch the DOL from Swiss. Quit returns to Swiss via the libogc2 exit stub
   when present.
4. Boot markers: `OGC stack=libogc2 fat=libdvm (Swiss)`,
   `FAT volume ready sd:/` (or `carda:/` / `cardb:/`),
   `FAT preferred volume …`.

Dolphin remains the day-to-day probe host; Swiss is the retail/hardware path.

## Audio / storage notes

| Subsystem | Current | Notes |
|-----------|---------|-------|
| Video | GX / Flipper | Unchanged |
| Input | libogc `PAD_` | Unchanged |
| Audio | ASND (`-lasnd`) | libansnd is a future optional DSP path |
| FAT / SD | `fatInitDefault()` | libdvm provides the `-lfat` API on the Swiss stack |
| Disc | libiso9660 | Unchanged |

## Force classic libogc

```bash
export XASH_GAMECUBE_OGC_STACK=libogc
scripts/build-gamecube.sh
```

## Outdated alternatives

Older drafts mentioned CMake toolchains, `powerpc-gekko-gcc`, and a grab-bag of
Wii portlibs (`libmikmod`, `freetype`, etc.). Those are **not** the supported
path for this repo. Use Waf + `scripts/build-gamecube.sh` as above.
