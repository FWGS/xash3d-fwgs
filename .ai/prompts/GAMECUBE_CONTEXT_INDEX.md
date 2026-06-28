# GameCube Context Index

Qwable-5 is a general coding model. It should treat this repository's
GameCube notes, source files, verifier output, and Dolphin logs as the source
of truth for this port.

Always prefer:

- `.ai/prompts/GAMECUBE_LOCAL_MISSION.md` for the local Qwable/Aider mission,
  evidence-first loop, and docs-only completion guardrails.
- Current source under `engine/platform/gamecube/`.
- Existing platform abstractions in `engine/platform/`, `engine/common/`, and
  backend selectors.
- `docs/GAMECUBE_PORT_PLAN.md` for verified state and blockers.
- `docs/GAMECUBE_HARDWARE_MATRIX.md` for supported, recommended,
  diagnostic, and unsupported hardware/loader routes.
- `docs/GAMECUBE_HARDWARE_VALIDATION.md` for manual hardware test protocol,
  evidence templates, failure taxonomy, and hardware-complete rules.
- `.ai/goals/GAMECUBE_PORT_GOALS.md` for acceptance criteria.
- `.ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md` for clean-room homebrew
  release, hardware, save-safety, packaging, and UX requirements.
- Per-subsystem notes selected by `scripts/ai-goal-loop.py`.

Do not assume desktop POSIX, OpenGL, dynamic libraries, writable install
directories, little-endian data, or unlimited memory.

When updating goals or the port plan, keep status precise:

- `source-complete`: code and local verifier evidence are sufficient.
- `Dolphin-smoke-complete`: emulator evidence reached the stated marker.
- `hardware-complete`: dated real hardware evidence exists using the hardware
  validation protocol.

Manual hardware criteria must be linked to the relevant manual goal instead of
being retried by automation. Do not mark hardware validation complete from
Dolphin-only evidence.
