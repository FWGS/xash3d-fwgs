You are editing /home/tim/Desktop/xash3d-gc.

Do exactly one small patch.

Current mission:
Make the GameCube port harness smarter and safer, not the engine itself.

Allowed edit files:
- scripts/ai-verify.sh
- scripts/ai-review.sh
- docs/GAMECUBE_PORT_PLAN.md

Task:
Improve the verification/review flow so future Aider passes are safer.

Requirements:
- scripts/ai-verify.sh should print clearer build-probe sections.
- scripts/ai-review.sh should reject patches larger than 400 changed lines.
- scripts/ai-review.sh should reject deleted files.
- scripts/ai-review.sh should require docs/GAMECUBE_PORT_PLAN.md to be updated.
- docs/GAMECUBE_PORT_PLAN.md should record that the AI harness now has a verifier/review gate.
- Do not edit engine source files.
- Stop after this one patch.
