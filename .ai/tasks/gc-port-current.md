Auto-port task for Xash3D GameCube
===================================

Failed phase: dolphin_boot
Failure kind: runtime_or_unknown
Patch targets: ['engine/platform/gamecube/in_gamecube.c', 'engine/client/cl_scrn.c']
Log path: .ai/logs/supervisor/dolphin_boot.log

Rules:
- Patch only the first target unless the error requires a header/source pair.
- Do not touch generated build/ files.
- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.
- Ignore public/miniz.c pragma notes.
- Keep the patch small and compile/probe-driven.

Error context:
--------------
HOST_FAILURE: another Dolphin boot probe is already running.
Terminated                 exit $?
Terminated                 exit $?


Automation pass rules:
- Patch only the first named target unless a header/source pair is required.
- Do not touch generated build/ files.
- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.
- Ignore public/miniz.c pragma notes.
- Keep the patch small and compile/probe-driven.
- There is no interactive human; do not ask questions.
