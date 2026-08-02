Auto-port task for Xash3D GameCube
===================================

Failed phase: dolphin_boot
Failure kind: runtime_probe
Patch targets: ['engine/platform/gamecube/in_gamecube.c', 'engine/client/cl_scrn.c', 'ref/gx/r_main.c']
Log path: .ai/logs/supervisor/dolphin_boot.log

Rules:
- Patch only the first target unless the error requires a header/source pair.
- Do not touch generated build/ files.
- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.
- Ignore public/miniz.c pragma notes.
- Keep the patch small and compile/probe-driven.

Error context:
--------------
==> Building GameCube engine and DOL...
==> Skipping engine rebuild (DOLPHIN_SKIP_BUILD=1); rebuilding disc image...
==> Building GameCube disc image...
  lean studio v_crowbar.mdl: 47356 → 18912 bytes
  lean studio v_9mmhandgun.mdl: 134016 → 61352 bytes
  lean studio w_crowbar.mdl: 2472 → 2472 bytes
GameCube studio mirror: injected 3 MDL(s) into bootstrap pk3
GameCube HUD mirror: injected 5 sprite(s) into bootstrap pk3
xorriso 1.5.6 : RockRidge filesystem manipulator, libburnia project.

Built /home/tim/Desktop/xash3d-gc/.ai/logs/dolphin-probe-20260801-193402/xash3d-gc.iso (91680768 bytes, hybrid GameCube/ISO9660)
Handoff metadata: /home/tim/Desktop/xash3d-gc/.ai/logs/dolphin-probe-20260801-193402/xash3d-gc-handoff.txt
==> Launching bounded Dolphin boot probe (300s, MMU=True)...
==> Analyzing probe results...
GUEST_FAILURE: Bootstrap was followed by a guest-engine error.
Logs: .ai/logs/dolphin-probe-20260801-193402
FRAME_BUDGET_STATS: samples=0 avg=0.00ms p95=0.00ms max=0.00ms target=16.67ms
BOOT_PHASE: sw_fb
HARNESS_FAILURE_LABEL: GUEST_RUNTIME_ERROR
HARNESS_FAILURE_SUMMARY: Guest runtime error markers were observed.
G36_STATUS: FAIL
G36_SUMMARY: guest error observed after bootstrap; map_loaded=no; input=yes; visual=unknown
G45_STATUS: FAIL
G45_SUMMARY: guest error observed during controller probe; map_loaded=no; input=yes
G45_ACTION_STATUS: WAIT
G45_ACTION_SUMMARY: none
VISUAL_STATUS: unknown


Automation pass rules:
- Patch only the first named target unless a header/source pair is required.
- Do not touch generated build/ files.
- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.
- Ignore public/miniz.c pragma notes.
- Keep the patch small and compile/probe-driven.
- There is no interactive human; do not ask questions.
