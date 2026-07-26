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
G403: Build real server implementation

**COMPLETED**

Implemented GameCube server with:
- Real HLSDK server archive (libhl_gamecube_ppc.a) linked
- Server exports integrated via setup_gamecube_server_exports()
- Server module uses real HLSDK implementation (not stub)
- Client and menu modules use real implementations
- Ref still uses real ref_gx implementation
- Audio and input remain stubs (not yet implemented)

**BUILD STATE VERIFICATION**
- Removed stub/server/server_export.c from build
- Removed stub/client, stub/server, stub/pm_shared stub files from build
- Server now uses real HLSDK server archive
- Build completes successfully with real server implementation

**VERIFICATION**
- Build: `python3 waf configure --gamecube && python3 waf build --gamecube` - SUCCESS
- Install: `python3 waf install --gamecube --destdir=OUT` - SUCCESS
- DOL generated: OUT/bin/boot.dol (3.9M)
- ELF generated: OUT/bin/xash (20M)
- Server init messages visible in output: "server init ready", "engine subsystems ready"

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.