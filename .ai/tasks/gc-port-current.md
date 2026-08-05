Auto-port task for Xash3D GameCube
=================================

Current goal:
G509: Expand soak coverage to a representative changelevel route

**IN PROGRESS (G509 soak gate)**:
- Extended gamecube-soak-probe.py with --g509 / --changelevel-route FROM:TO
- Default continuity route: c0a0 → c0a0a with G68/G100 requirements
- Wrapper: scripts/gamecube-g509-soak.sh
- Release packet prefers changelevel-mode soak reports
- Host dry-run + parser tests added

Next acceptance step:
- Run `scripts/gamecube-g509-soak.sh` against a real Dolphin build
- Archive report.json beside the same-build G508 probe evidence
- Keep physical SD fault cases hardware-only

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
