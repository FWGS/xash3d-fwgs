Auto-port task for Xash3D GameCube
===================================

Failed phase: runtime_regression
Failure kind: runtime_or_unknown
Patch targets: ['engine/client/cl_scrn.c', 'ref/gx/r_main.c']
Log path: .ai/logs/supervisor/runtime_regression.log

Rules:
- Patch only the first target unless the error requires a header/source pair.
- Do not touch generated build/ files.
- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.
- Ignore public/miniz.c pragma notes.
- Keep the patch small and compile/probe-driven.

Error context:
--------------
runtime gate: FAIL
runtime gate: logs=.ai/logs/dolphin-probe-20260710-233634
- missing: state status is map_ready
- missing: c0a0e map loaded
- missing: direct map reached ready marker


Automation pass rules:
- Patch only the first named target unless a header/source pair is required.
- Do not touch generated build/ files.
- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.
- Ignore public/miniz.c pragma notes.
- Keep the patch small and compile/probe-driven.
- There is no interactive human; do not ask questions.
