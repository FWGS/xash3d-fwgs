# G491 SV_InitGame entry experiment

- Parent commit: `b10e1e938f`
- Hypothesis: the server startup stall occurs before `SV_InitGame` reaches its
  first filesystem/module operation.
- Changed files: `engine/server/sv_init.c`
- Build: PASS — `bash scripts/build-gamecube.sh`; fresh DOL/disc produced.
- Probe: `.ai/logs/dolphin-probe-20260802-194638/`; G45 passed, but the last
  marker remained `server game init begin`.
- Result: the fresh DOL contains the entry/reset/path marker strings, yet none
  were emitted. G201 delta diagnostics were not reached.
- Revert/keep: keep only as diagnostic evidence; accept no source fix. Next
  experiment needs a call-site/ABI or guest-trace target.
