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
- For G36, distinguish missing telemetry from over-budget telemetry. If the
  latest harness reports captured frame samples around 67ms, do not make
  generic clock cleanup; reduce the GameCube smoke/render path cost or improve
  real frame pacing evidence while preserving MAP_READY/G45/nonblack output.
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

---

## Additional Technical Goals

### Module Linkage & Stub Replacement
- [ ] Audit stub/client, stub/server, stub/menu, stub/pm_shared for real implementations
- [ ] Replace stub/client with real HLSDK client_gamecube_ppc.a when available
- [ ] Replace stub/server with real HLSDK hl_gamecube_ppc.a when available
- [ ] Replace stub/menu with real menu implementation from ref/gx or HLSDK
- [ ] Replace stub/pm_shared with real pm_shared implementation from pm_shared directory
- [ ] Verify all module exports match expected HLSDK interfaces
- [ ] Build GameCube target and inspect final ELF symbols to confirm real implementations
- [ ] Update GameCube roadmap with verified module-linkage matrix

### Build System & Configuration
- [ ] Verify wscript GameCube build configuration uses real HLSDK archives
- [ ] Ensure XASH_GAMECUBE_REQUIRE_HLSDK=0 is not set unintentionally
- [ ] Verify DEVKITPRO/libogc paths are correctly configured
- [ ] Confirm ref_gx is built as sibling static lib target
- [ ] Verify all required libraries (fat, snd, ogc, m, iso9660) are linked
- [ ] Ensure proper symbol renaming for HLSDK archives (gamecube_hlsdk_* prefixes)

### GameCube Platform Support
- [ ] Verify platform/gamecube/*.c implementations are complete
- [ ] Check platform/stub/s_stub.c for proper stub implementation
- [ ] Ensure GX renderer integration is correct for GameCube
- [ ] Verify SDL2/SDL3 configuration for GameCube (should be disabled for pure Flipper)
- [ ] Confirm soft/GL renderers are disabled for pure Flipper builds
- [ ] Verify low memory mode is enabled (XASH_LOW_MEMORY >= 1)

### Testing & Verification
- [ ] Build GameCube target and inspect final ELF symbols
- [ ] Run Dolphin emulator and verify non-black screen output
- [ ] Verify game loop execution (no infinite loops or crashes)
- [ ] Check frame pacing and render timing
- [ ] Verify input handling (controller, memory card, etc.)
- [ ] Test save/load functionality with memory card emulation
- [ ] Verify audio output through Dolphin's audio backend

### Documentation & Roadmap
- [ ] Update GameCube roadmap with verified module implementations
- [ ] Document stub-to-real migration status
- [ ] Record build configuration requirements (DEVKITPRO, libogc, HLSDK)
- [ ] Document known limitations and workarounds
- [ ] Add build verification steps to CI/CD pipeline
- [ ] Create release checklist for GameCube builds

### Release Engineering
- [ ] Verify ISO generation works (xash3d-gc-*.iso files)
- [ ] Ensure release artifacts are reproducible
- [ ] Check that release evidence is stored in .ai/logs/
- [ ] Verify RC gate passes (scripts/gamecube-rc-check.sh)
- [ ] Document release evidence chain (build -> ISO -> Dolphin test)
- [ ] Create automated release verification script

