# GameCube Homebrew Compliance Requirements

Version: 0.1  
Status: Draft  
Scope: Unofficial clean-room requirements for GameCube homebrew releases. This
is not Nintendo TRG/Lotcheck and is not affiliated with or endorsed by Nintendo.

## Purpose

The Xash3D GameCube port should boot reliably, avoid save corruption, recover
from hardware edge cases, and feel like a polished console title on real
hardware, Swiss, SD2SP2/SD Gecko, ODEs, and emulators.

## Requirements

| Area | Requirement |
| --- | --- |
| Boot | Boot `boot.dol` and ISO/GCM builds through Dolphin/Swiss and eventually real hardware. Missing required files must show readable errors. |
| Emulator assumptions | Dolphin success is not release proof. Do not depend on Dolphin-only timing, filesystem, memory card, graphics, or uninitialized-memory behavior. |
| Video | Use valid NTSC/PAL modes, keep critical UI inside an 8-10% 4:3 safe area, keep text legible on CRT/analog capture, and never force 480p. |
| Controller | Detect Port 1 at boot, recover from disconnect/reconnect, use predictable port mapping, apply analog deadzones, and show GameCube button names. |
| Save safety | Never create, overwrite, delete, repair, or format without confirmation. Handle missing, full, removed, corrupt, wrong-slot, and incompatible cards. |
| Save integrity | Use magic, version, payload size, checksum/CRC32, and atomic temp/backup-style writes before enabling memory-card saves. |
| Filesystem | Use exact-case relative paths, no host-machine paths, no required writes beside the executable, and visible missing-asset errors. |
| Audio | Audio init failure is nonfatal. Streaming must tolerate disc/SD latency and avoid severe clipping. |
| Performance | Define a target frame rate, decouple gameplay timing from frame rate, show loading feedback after about 2 seconds, and record worst-case scene evidence. |
| Errors | Avoid silent black screens. Show readable fatal errors and crash breadcrumbs in test/debug builds. |
| UI/UX | Provide title/options/controls/pause/save/error screens as features mature. Confirm destructive actions with clear language. |
| Legal | Do not distribute Nintendo SDK files, proprietary docs, BIOS/IPL dumps, or copyrighted assets. Include license, credits, third-party notices, and an unofficial-homebrew disclaimer. |
| Packaging | Include version, README, license, credits, changelog, checksums, `boot.dol`, optional ISO/GCM, assets, controls, and troubleshooting notes. |
| Hardware matrix | Track Dolphin, Swiss SD2SP2/SD Gecko, real GameCube/Wii GC mode, Memory Card Slot A/B where applicable, official controller, WaveBird, third-party controller, no-controller, and disconnect tests. |
| Accessibility | Avoid rapid full-screen flashing, provide subtitles/visual equivalents for critical audio where possible, and support alternate control presets when feasible. |
| Network | If networking is enabled, no hardware/network absence may hard crash. Operations need timeouts and offline content should remain playable. |
| Developer evidence | Preserve logs or overlay data for FPS, frame time, MEM1/ARAM, current map, player position, active entities, loader path, build hash, and crash breadcrumbs. |

## Pre-Release Checklist

```text
[ ] Boots in Dolphin
[ ] Boots on real hardware
[ ] Boots through Swiss
[ ] No-controller state handled
[ ] Controller disconnect handled
[ ] Pause menu works
[ ] Title/menu text readable on CRT or analog capture
[ ] 480i works
[ ] 480p works or is safely disabled
[ ] Missing asset does not black-screen silently
[ ] Memory card missing handled
[ ] Memory card full handled
[ ] Save file uses checksum/version/magic
[ ] Save interruption tested
[ ] No unrelated card files touched
[ ] No Nintendo SDK/proprietary files included
[ ] No ripped commercial assets included
[ ] Version shown in game
[ ] README included
[ ] License included
[ ] Credits included
[ ] Release checksum generated
```

## Automation Policy

`scripts/gamecube-homebrew-compliance-check.py` is a lightweight pipeline gate.
It can verify that the compliance documents and model context are present, that
release goals carry compliance language, and that obvious proprietary SDK
strings are not added. It does not prove real hardware behavior.

Use normal mode during development:

```sh
scripts/gamecube-homebrew-compliance-check.py
```

Use strict mode only near release or hardware validation:

```sh
scripts/gamecube-homebrew-compliance-check.py --strict
```

Strict mode expects recorded Dolphin/hardware/package evidence and should remain
a release-quality gate rather than a blocker for early engine bring-up.
