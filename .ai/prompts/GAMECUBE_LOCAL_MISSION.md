# GameCube Local Mission

Mission: finish a clean-room, native GameCube Xash3D/Half-Life 1 port using
devkitPPC/libogc, local evidence, and small source patches.

Qwable-5 and the Aider GUI are the local porting cockpit. They should work in
this repeatable loop:

1. Gather compact Dolphin evidence, ConAct/mempalace state, verifier output,
   RC logs, source context, and the goal ledger.
2. Summarize the current blocker as structured evidence, not a vague TODO.
3. Make one surgical source-first patch. Verifier/release-evidence patches are
   allowed when the gate itself is missing.
4. Run the narrow verifier, build, RC gate, or Dolphin probe needed for that
   goal.
5. Feed the proof back into ConAct/mempalace and commit only when the patch
   changes behavior or durable release evidence.

For G36 and later, accepted commits must satisfy at least one of these:

- Change GameCube source behavior.
- Add or harden a verifier, RC gate, or reproducible release evidence.
- Update release/hardware documentation with dated operator evidence.

Avoid these failure modes:

- Do not mark a goal complete from reasoning or docs-only claims.
- Do not retry the same broad prompt after a token/context failure.
- Do not use the RTX Pro 6000 to make prompts huge. Use it for larger local
  review and stronger focused context, while keeping mutation passes bounded.
- Do not add probe-only changes unless the goal is explicitly missing probe
  evidence or the probe parser is wrong.
- Do not reopen stale blockers when newer Dolphin memory has advanced past
  them. Preserve active-rendering/nonblack evidence unless a newer run
  regresses.
- Do not claim hardware-complete from Dolphin evidence.
- Do not copy proprietary Nintendo SDK code, headers, text, assets, BIOS/IPL
  material, or local Half-Life content into Git or release packages.

If the required source file is not editable in the current Aider pass, improve
`scripts/ai-goal-loop.py` goal context or record the exact missing file/blocker.
That is better than producing victory documentation without source proof.

The current release-candidate gate is `scripts/gamecube-rc-check.sh`. Nothing
should advance toward release unless a verifier or RC log leaves durable
evidence under `.ai/logs/`.

The ideal local agent cycle is:

`Dolphin evidence -> ConAct/mempalace summary -> tiny source patch -> build -> Dolphin proof -> commit`
