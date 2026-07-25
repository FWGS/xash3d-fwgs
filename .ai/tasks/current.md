Continue the Xash3D GameCube port with one bounded patch.

Current mission (first open automatic goal):
G38 — Validate on physical GameCube hardware (manual checkpoint)

Status:
- Automation tier: hardware_validation
- G304/G305 closed (ambient+env_message; 41 inhibited)
- G197-G256 completed (Flipper GX renderer, landmark_changelevel milestone)
- G38 is the open checkpoint for manual hardware validation

Requirements:

- Boot the generated DOL or disc image through at least one real hardware method
- Record video output, controller input, storage, audio, map load, frame pacing
- Compare hardware behavior against Dolphin logs and split emulator-only bugs
- Use `scripts/gamecube-hardware-handoff.sh` to generate repeatable artifacts
- Record results in `docs/GAMECUBE_PORT_PLAN.md`
- Stop after this one validation session
