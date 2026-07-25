Auto-port task for Xash3D GameCube
=================================

Manual checkpoint:
G38 physical GameCube validation is MANUAL_VALIDATION_PENDING.

Selection policy:
- Skip manual-only goals when choosing autonomous implementation work.
- Do not mark manual goals complete without operator evidence.
- Prepare repeatable hardware artifacts and testing instructions.
- Continue with the first incomplete automatable goal in the goal ledger.

Current goal:
Determine the next incomplete non-manual GameCube port goal from:
- .ai/goals/GAMECUBE_PORT_GOALS.md
- docs/GAMECUBE_PORT_PLAN.md
- docs/gamecube/PORT_STATUS.md, when present

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
