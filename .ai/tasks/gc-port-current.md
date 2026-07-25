Auto-port task for Xash3D GameCube
===================================

Current goal: G38 Validate on physical GameCube hardware (manual checkpoint)

Status:
- Automation tier: hardware_validation
- G304/G305 closed (ambient+env_message; 41 inhibited)
- G197-G256 completed (Flipper GX renderer, landmark_changelevel milestone)
- G38 is the open checkpoint for manual hardware validation

Rules:
- Manual hardware validation task
- Use `scripts/gamecube-hardware-handoff.sh` for repeatable artifacts
- Record results in `docs/GAMECUBE_PORT_PLAN.md`
- Do not touch generated build/ files
- Keep the validation focused and evidence-driven

Acceptance:
- Boot DOL or disc image on real hardware
- Record video output, controller input, storage, audio, map load, frame pacing
- Compare hardware behavior against Dolphin logs
- Document results in port plan

Verify:
scripts/gamecube-hardware-handoff.sh
