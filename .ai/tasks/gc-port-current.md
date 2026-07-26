Auto-port task for Xash3D GameCube
=================================

Manual checkpoint:
G38 physical GameCube validation is MANUAL_VALIDATION_PENDING.

Selection policy:
- Skip manual-only goals when choosing autonomous implementation work.
- Do not mark manual goals complete without operator evidence.
- Prepare repeatable hardware artifacts and testing instructions.
- Continue with the first incomplete automatable goal in the goal ledger.

Current goal:
G405: Build real combined implementation

**IN PROGRESS**

Goal: Integrate menu, server, and client into unified build

**COMPLETED (G404)**:
- Client uses real HLSDK client archive (libclient_gamecube_ppc.a)
- Server uses real HLSDK server archive (libhl_gamecube_ppc.a)
- Menu uses real implementation
- Ref uses real ref_gx implementation
- Build verified: boot.dol (3.9M), xash (20M)
- No stub references in build output

**NEXT STEPS**:
- Verify all components work together in unified build
- Test combined system functionality
- Document integration architecture
- Create build configuration for combined build

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.