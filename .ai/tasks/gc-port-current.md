Auto-port task for Xash3D GameCube
===================================

Failed phase: build_disc
Failure kind: runtime_or_unknown
Patch targets: ['scripts/build-gamecube-disc.py']
Log path: .ai/logs/supervisor/build_disc.log

Rules:
- Patch only the first target unless the error requires a header/source pair.
- Do not touch generated build/ files.
- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.
- Ignore public/miniz.c pragma notes.
- Keep the patch small and compile/probe-driven.

Error context:
--------------
Staging retail Half-Life assets for GameCube (source files are not modified).
Retail staging: omitted 27 unsupported file(s) (.mp3, .mpeg, .mpg, .ogg, .wmv).
GameCube menu assets: baked resource/gc_menu/background.tga from HD_BackgroundLayout.txt (105 tiles, nonblack=53.4%)
/home/tim/Desktop/xash3d-gc/scripts/build-gamecube-disc.py:768: DeprecationWarning: Image.Image.getdata is deprecated and will be removed in Pillow 14 (2027-10-15). Use get_flattened_data instead.
  datas = char.getdata()
/home/tim/Desktop/xash3d-gc/scripts/build-gamecube-disc.py:797: DeprecationWarning: Image.Image.getdata is deprecated and will be removed in Pillow 14 (2027-10-15). Use get_flattened_data instead.
  datas = char.getdata()
GameCube loading assets: baked resource/gc_menu/loading.tga and resource/gc_menu/intro.tga (bald scientist + HL motif)
Built static-hold GCVID logo.gcvid from frame 80 (320x48, duration 110 frames, rgb565)
Built static-hold GCVID valve.gcvid from frame 80 (320x240, duration 150 frames, rgb565)
  lean studio v_crowbar.mdl: 47356 → 18912 bytes
  lean studio v_9mmhandgun.mdl: 134016 → 61352 bytes
  lean studio w_crowbar.mdl: 2472 → 2472 bytes
  lean studio roach.mdl: 6840 → 3812 bytes
GameCube studio mirror: injected 4 MDL(s) into bootstrap pk3
GameCube HUD mirror: injected 5 sprite(s) into bootstrap pk3
  lean font CREDITSFONT: → gfx/gc_creditsfont.fnt (5908 bytes, 128x32)
GameCube font mirror: injected 1 lean font(s) into bootstrap pk3
GameCube sky mirror: injected 4 lean BMP(s) into bootstrap pk3
xorriso 1.5.6 : RockRidge filesystem manipulator, libburnia project.


UNIX-SIGNAL:  SIGTERM  errno= 2
/usr/bin/xorriso : ABORT : Trying to shut down busy drives
/usr/bin/xorriso : ABORT : Wait the normal burning time before any kill -9
libburn : ABORT : Urged drive worker threads to do emergency halt
libisofs: MISHAP : Image write cancelled
xorriso : ABORT : Drive is released and library is shut down now.
xorriso : ABORT : Program done. Even if you do not see a shell prompt.



Automation pass rules:
- Patch only the first named target unless a header/source pair is required.
- Do not touch generated build/ files.
- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.
- Ignore public/miniz.c pragma notes.
- Keep the patch small and compile/probe-driven.
- There is no interactive human; do not ask questions.
