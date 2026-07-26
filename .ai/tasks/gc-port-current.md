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
G402: Build real menu implementation

**COMPLETED**

Implemented GameCube menu with:
- 5 menu items: New Game, Load Game, Options, Controller, System
- D-pad and A/B/Start button support for navigation
- Visual menu rendering with descriptions
- Proper key destination management
- Integration with engine menu interface

**BUILD STATE VERIFICATION**
- Menu module now uses real implementation (not stub)
- Client and server still use real HLSDK implementations
- Ref still uses real ref_gx implementation
- Audio and input remain stubs (not yet implemented)

**VERIFICATION**
- menu_stub.c replaced with full menu implementation
- Menu includes GameCube-specific items (Controller, System)
- Navigation supports K_UPARROW, K_DPAD_UP, K_DOWNARROW, K_DPAD_DOWN
- Selection supports K_ENTER, K_A_BUTTON, K_START_BUTTON
- Exit supports K_ESCAPE, K_B_BUTTON

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.