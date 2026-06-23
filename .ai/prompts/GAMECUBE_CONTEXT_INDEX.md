# GameCube Context Index

Qwable-5 is a general coding model. It should treat this repository's
GameCube notes, source files, verifier output, and Dolphin logs as the source
of truth for this port.

Always prefer:

- Current source under `engine/platform/gamecube/`.
- Existing platform abstractions in `engine/platform/`, `engine/common/`, and
  backend selectors.
- `docs/GAMECUBE_PORT_PLAN.md` for verified state and blockers.
- `.ai/goals/GAMECUBE_PORT_GOALS.md` for acceptance criteria.
- Per-subsystem notes selected by `scripts/ai-goal-loop.py`.

Do not assume desktop POSIX, OpenGL, dynamic libraries, writable install
directories, little-endian data, or unlimited memory.
