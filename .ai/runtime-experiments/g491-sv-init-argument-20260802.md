# G491 SV_InitGame argument boundary — 2026-08-02

- Parent commit: `9f18dd721f`
- Hypothesis: the apparent `SV_InitGame` hang occurs while evaluating the
  `GI->gamemode` argument in `SV_Init`, before the call reaches the server
  initialization function.
- Changed files: `engine/server/sv_main.c` only.
- Build result: PASS. `scripts/build-gamecube.sh` produced a fresh ELF, valid
  DOL, and disc image.
- Probe result: `.ai/logs/dolphin-probe-20260802-195057/`.
  The guest emitted `server game init begin` and
  `server game init argument begin`, but did not emit
  `server game init argument ready`. It did not emit `SV_InitGame entry`.
- Static confirmation: `SV_Init` contains a direct branch from `0x800b3d18`
  to `SV_InitGame` at `0x800afec8`. The callee's first report call is at
  `0x800afeec`, so the missing callee marker is consistent with the argument
  evaluation boundary, not a missing link symbol.
- Keep/revert decision: keep diagnostic instrumentation; do not accept a
  functional fix. The current blocker is invalid/unusable `FI.GameInfo`/`GI`
  state at the `gamemode` read, or a guest memory/ABI failure at that access.

## Next targeted experiment

Instrument the filesystem/gameinfo handoff immediately before `SV_Init` and
report the `FI.GameInfo` pointer and safe scalar fields already known to be
valid. Then determine where `FI.GameInfo` is initialized and whether the
pointer lies in a valid MEM1/MEM2 range. Do not enter `SV_InitGame`, rework
Delta_Init, or invoke re_agent until a concrete address/ABI mismatch is
captured.
