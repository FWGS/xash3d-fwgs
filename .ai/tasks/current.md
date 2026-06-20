Continue the Xash3D GameCube port.

Read `docs/GAMECUBE_PORT_PLAN.md`, the latest Git log and diff, and the
read-only project rules. Do exactly one useful, low-risk patch toward the next
documented blocker.

Rules:

- Keep the patch small and limited to one demonstrated blocker.
- Do not break existing targets.
- Prefer platform isolation and existing abstractions.
- Do not fake unsupported systems or attempt a large renderer rewrite.
- Update `docs/GAMECUBE_PORT_PLAN.md` with what changed, the exact commands
  tried and results, and the next blocker.
- Stop after one patch.
