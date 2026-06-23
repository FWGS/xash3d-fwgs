# GameCube Homebrew Compliance Profile

This is a clean-room, public homebrew quality bar for the Xash3D GameCube port.
It is not Nintendo TRG/Lotcheck and must never rely on proprietary SDK material.

Treat these rules as release and hardware-goal requirements:

- Boot: produce `boot.dol` and optional ISO/GCM builds that boot through Swiss,
  SD2SP2/SD Gecko, Dolphin, and eventually real DOL-001/DOL-101 or Wii
  GameCube mode. Missing files must show readable errors instead of black
  screens.
- Video: support safe 4:3 title area, legible CRT-sized text, valid NTSC/PAL
  modes, and optional 480p only when user-selectable or safely disabled.
- Controller: default to Port 1, handle no-controller and disconnect/reconnect
  states, use GameCube button names, and apply stick/trigger deadzones.
- Save safety: never write/delete/format without confirmation. Handle missing,
  wrong-slot, full, removed, corrupt, and incompatible cards. Use magic,
  version, size, checksum, and atomic temp/backup-style writes before enabling
  real memory-card saves.
- Filesystem: fail visibly on missing assets, keep exact-case relative paths,
  never depend on host-machine paths, and assume the boot medium can be
  read-only.
- Audio: audio init failure must be nonfatal. Streaming must tolerate SD/disc
  latency and avoid severe clipping.
- Performance: define a target frame rate, keep gameplay timing independent of
  frame rate, show feedback for long loads, and record real hardware worst-case
  scene evidence before release.
- Errors: avoid silent black-screen failures. Debug builds may show detailed
  breadcrumbs; release builds should show user-readable fatal errors.
- UX: provide title/options/controls/pause/error screens as the port matures.
  Confirm destructive actions and keep A confirm, B cancel/back, Start pause.
- Legal/package: distribute no Nintendo SDK files, proprietary docs, BIOS/IPL
  dumps, or copyrighted game assets. Include license, credits, notices,
  version, checksums, and an unofficial-homebrew disclaimer.
- Hardware matrix: before release, record Dolphin, Swiss, real hardware, memory
  card, official controller, WaveBird/third-party where possible, no-controller,
  and mid-game disconnect results.
- Developer evidence: keep a debug overlay or equivalent logs for FPS, memory,
  current map, loader path, build hash, and crash breadcrumbs. Maintain a
  compliance test map or scripted equivalent with controller, text, save, audio,
  texture, alpha, lighting, particle, loading, camera, and error stations.

Autonomous passes must not mark release/hardware compliance complete from
source inspection alone. They need verifier output, Dolphin logs, package
artifacts, or operator-recorded hardware evidence.
