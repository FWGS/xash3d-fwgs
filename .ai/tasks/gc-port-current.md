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
G404: Build real client implementation

**COMPLETED**

Implemented GameCube client with:
- Real HLSDK client archive (libclient_gamecube_ppc.a) linked
- Client exports integrated via setup_gamecube_client_exports()
- Client module uses real HLSDK implementation (not stub)
- Server and menu modules use real implementations
- Ref still uses real ref_gx implementation
- Audio and input remain stubs (not yet implemented)

**BUILD STATE VERIFICATION**
- Client uses real HLSDK client archive (XASH_GAMECUBE_HLSDK_STATIC=1)
- Build uses -lclient_gamecube_ppc linker flag
- No stub/client references in build output
- Build completes successfully with real client implementation

**VERIFICATION**
- Build: `python3 waf configure --gamecube && python3 waf build --gamecube` - SUCCESS
- Install: `python3 waf install --gamecube --destdir=OUT` - SUCCESS
- DOL generated: OUT/bin/boot.dol (3.9M)
- ELF generated: OUT/bin/xash (20M)
- Client and server archives linked: "Using GameCube HLSDK client archive", "Using GameCube HLSDK server archive"

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.