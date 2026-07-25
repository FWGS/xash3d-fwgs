Continue the Xash3D GameCube port with one bounded, automatable patch.

Manual checkpoint:
G38 physical GameCube validation is pending operator execution. Prepare its
artifacts and checklist, but do not stop automation solely because physical
hardware is unavailable.

Current autonomous mission:
Inspect the goal ledger and port plan, skip goals explicitly classified as
manual-only, and select the first incomplete goal that can be implemented,
built, tested, or documented from this repository and Dolphin environment.

Requirements:
- Do not falsely complete G38.
- Record G38 as MANUAL_VALIDATION_PENDING.
- Generate or update the hardware handoff package if needed.
- Select the next automatable incomplete goal.
- Make one bounded patch.
- Build and run the relevant verification.
- Update docs/GAMECUBE_PORT_PLAN.md and durable task state.
- Commit only after validation.
