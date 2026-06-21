Continue the Xash3D GameCube port with one bounded patch.

Current mission:
Fix or audit the GameCube build warning around `SV_InitEdict` without changing
ABI-sensitive entity layout.

Requirements:

- Reproduce and inspect the `-Wstringop-overflow` warning before editing.
- If it is a false positive, isolate suppression to `SV_InitEdict` only and
  document why it is safe.
- If it is a real overflow risk, fix the size/source-object mismatch without
  changing ABI-sensitive entity layout.
- Do not hide unrelated warnings or weaken project-wide warning flags.
- Keep the patch small and preserve existing non-GameCube targets.
- Update `docs/GAMECUBE_PORT_PLAN.md` with commands, evidence, and next steps.
- Run the relevant verifier and stop after this one patch.
