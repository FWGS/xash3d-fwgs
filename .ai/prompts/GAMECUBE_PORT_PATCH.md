# GameCube Port Patch Pass

Make one small source patch for the named target file only.

Rules:
- Fix the failed supervisor phase using the error context below.
- Do not edit harness scripts, docs, or unrelated engine files.
- Do not touch `engine/platform/gamecube/vid_gamecube.c` unless the error names it.
- Rebuild is verified by automation after your commit; keep the diff minimal.
- For memory failures: trim allocations, release staging buffers, or reuse scratch.
- For runtime/input failures: preserve MAP_READY, G45, and nonblack visual output.
