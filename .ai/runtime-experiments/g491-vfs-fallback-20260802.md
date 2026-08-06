# G491 filesystem fallback experiment

- Parent commit: `8507d2e91b`
- Hypothesis: the read-only missing-`vfs.cfg` fallback hangs in
  `FS_Rescan_f()` before server/map initialization.
- Changed files: `engine/common/filesystem_engine.c`,
  `engine/server/sv_init.c`
- Build: PASS — `bash scripts/build-gamecube.sh`; valid DOL and smoke ISO.
- ISO check: PASS — `xash3d/valve/delta.lst`, 12,567 bytes.
- Probe 1: `.ai/logs/dolphin-probe-20260802-191640/`; stalled before engine
  readiness at missing-`vfs.cfg` fallback.
- Probe 2: `.ai/logs/dolphin-probe-20260802-192020/`; `vfs fallback rescan
  begin` observed, `vfs fallback rescan done` absent.
- Probe 3: `.ai/logs/dolphin-probe-20260802-192224/`; after skipping rescan,
  fallback completed, G45 passed, then `SV_InitGame` stalled before G201.
- Delta result: NOT REACHED — `FS_FileExists`/`FS_LoadFile` diagnostic did not
  execute.
- Revert/keep: keep as an isolated experiment only; do not accept or generate a
  release fix until the next server-initialization boundary is isolated.
