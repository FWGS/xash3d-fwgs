# Xash3D GameCube Release Manifest and Legal Audit

## Unofficial Homebrew Disclaimer

This software is an unofficial homebrew port of the Xash3D engine to the Nintendo GameCube.
It is not affiliated with, endorsed by, or supported by Valve Corporation, Nintendo, or any of their subsidiaries.

Half-Life, the Half-Life logo, and related trademarks are property of Valve Corporation.
GameCube is a trademark of Nintendo.

## Legal Audit

### Proprietary Assets
This release package **does not contain**:
- Nintendo SDK files, headers, or libraries.
- BIOS, IPL, or firmware dumps.
- Copyrighted game assets (models, textures, sounds, maps) from Half-Life or other titles.

### Included Software
- **Xash3D Engine:** BSD 3-Clause License.
- **devkitPPC/libogc:** GPL v2.
- **Third-Party Libraries:** See `THIRD-PARTY-NOTICES.txt` in the root of the extracted source or release archive.

### Distribution
You may redistribute this engine binary (`boot.dol`) and source code under the terms of their respective licenses.
You **may not** redistribute copyrighted game content. Users must provide their own legally acquired Half-Life assets.

## Release Package Contents

A standard release package should include:
1. `boot.dol` - The executable GameCube application.
2. `README.md` - Basic usage instructions.
3. `LICENSE` - License information for the engine and port.
4. `THIRD-PARTY-NOTICES.txt` - Licenses for all third-party components.
5. `CHANGES.md` - Changelog for this version.

## Local Asset Staging Instructions

To run this software, you must provide Half-Life game assets:

1. Obtain a legal copy of Half-Life.
2. Create the directory structure `xash3d/valve` on your SD card or in your GameCube ISO filesystem.
3. Copy the following critical directories/files from your Half-Life installation to `xash3d/valve`:
   - `gfx.wad`
   - `valve.rc`
   - `default.cfg`
   - `config.cfg`
   - `gfx/` (palette.lmp, conback.lmp, colormap.lmp)
   - `maps/` (e.g., `c0a0e.bsp`)
   - `models/`
   - `sprites/`
   - `sound/`
4. Ensure filenames are lowercase. The GameCube filesystem is case-sensitive for these paths.

## Controls

- **Main Stick:** Look / Move
- **C-Stick:** Look (secondary)
- **A Button:** Confirm / Use
- **B Button:** Cancel / Back
- **X Button:** Alt Fire / Attack 2
- **Y Button:** Jump
- **Z Button:** Attack 1
- **L/R Buttons:** Trigger inputs
- **Start:** Pause / Menu
- **D-Pad:** Weapon select / Impulse commands

## Troubleshooting

- **Black Screen:** Ensure `boot.dol` is placed correctly. Verify SD card is FAT32 formatted.
- **Missing Textures:** Check that `gfx.wad` and `gfx/` directory are present in `xash3d/valve`.
- **No Sound:** The port uses a null or basic ASND backend. Ensure audio is enabled in console settings.
- **Crashes on Map Load:** Some maps may exceed the GameCube's 24MB memory limit. Try smaller maps like `c0a0e` or `c1a0`.

## Build Verification

- **Build Hash:** Generated per-release via `scripts/build-gamecube.sh`.
- **Artifact Hashes:** `boot.dol` and ISO hashes should be recorded in `OUT/release-hashes.txt` after build.
