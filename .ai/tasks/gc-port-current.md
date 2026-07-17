Auto-port task for Xash3D GameCube
===================================

Current goal: G83 Fix GameCube BSP PointInLeaf and parent-cycle PVS
Patch targets: ['engine/common/mod_bmodel.c', 'ref/gx/r_main.c', 'ref/gx/r_misc.c']

Rules:
- Patch only the first target unless the error requires a header/source pair.
- Do not touch generated build/ files.
- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.
- Ignore public/miniz.c pragma notes.
- Keep the patch small and compile/probe-driven.

Acceptance:
- Mod_PointInLeaf + MarkLeaves/FatPVS work on -gcnewgame c0a0 without hang
- MAP_READY + G36 PASS + nonzero world pixels retained
- Evidence in docs/GAMECUBE_PORT_PLAN.md

Verify:
DOLPHIN_NEWGAME=1 DOLPHIN_TIMEOUT=120 scripts/dolphin-boot-probe.sh
