# G491 filesystem lookup/load contract — 2026-08-02

- Parent commit: `49fa359a17`
- Hypothesis: `FS_FileExists` made later loads fail because the GameCube
  successful-lookup cache was treated as a negative result by `FS_FindFile`.
- Runtime diagnostic files: `filesystem/searchpath.c` plus temporary logging in
  `filesystem/io.c`; the temporary logging was removed before commit.
- Fix: stop returning `NULL` when a name is present in the successful-lookup
  cache. The cache stores only names, not the searchpath/index needed to load
  the file, so every lookup must still resolve its actual searchpath.
- Also clear stale hit/miss caches from `FS_ClearSearchPath` whenever mounts
  are rebuilt.
- Build result: PASS. Fresh ELF, valid DOL, and smoke disc generated.
- Probe result: `.ai/logs/dolphin-probe-20260802-200351/`.
  `delta.lst`: `FS_FileExists=1`, `FS_LoadFile=1`, size `12567`, disk
  `gcdisc:/xash3d/valve/delta.lst`.
  `valve/delta.lst`: `FS_FileExists=1`, `FS_LoadFile=1`, size `12567`, same
  disk path.
- Runtime gates: G201 delta reinit ready; BSP loaded; entity spawn completed;
  `map loaded c0a0e`; stable rendered frames reached; no guest fatal marker.
- Keep/revert decision: keep the cache contract fix; revert temporary `io.c`
  logging. This experiment is accepted for the filesystem contract, while
  later gameplay/entity warnings remain separate work.
