# GameCube Local Mission

Mission: finish a clean-room, native GameCube Xash3D/Half-Life 1 port using
devkitPPC/libogc, local evidence, and small source patches.

Qwable-5 and the Aider GUI should work in this loop:

1. Gather current evidence from source, verifiers, RC logs, Dolphin logs, and
   the goal ledger.
2. Make one surgical patch that changes source behavior or adds a missing
   verifier/evidence gate.
3. Run the narrow verifier or RC gate needed for that goal.
4. Update the goal ledger and port plan only with command output, log paths,
   or explicit manual/hardware blockers.

For G36 and later, accepted commits must satisfy at least one of these:

- Change GameCube source behavior.
- Add or harden a verifier, RC gate, or reproducible release evidence.
- Update release/hardware documentation with dated operator evidence.

Avoid these failure modes:

- Do not mark a goal complete from reasoning or docs-only claims.
- Do not retry the same broad prompt after a token/context failure.
- Do not add probe-only changes unless the goal is explicitly missing probe
  evidence or the probe parser is wrong.
- Do not claim hardware-complete from Dolphin evidence.
- Do not copy proprietary Nintendo SDK code, headers, text, assets, BIOS/IPL
  material, or local Half-Life content into Git or release packages.

If the required source file is not editable in the current Aider pass, improve
`scripts/ai-goal-loop.py` goal context or record the exact missing file/blocker.
That is better than producing victory documentation without source proof.

The current release-candidate gate is `scripts/gamecube-rc-check.sh`. Nothing
should advance toward release unless a verifier or RC log leaves durable
evidence under `.ai/logs/`.
