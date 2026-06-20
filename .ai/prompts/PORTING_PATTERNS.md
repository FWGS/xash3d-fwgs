# Relevant Porting Patterns

Use these existing paths as the first source of truth:

- `common/defaults.h` selects platform backends.
- `common/backends.h` defines backend identifiers.
- `engine/platform/platform.h` defines the platform contract.
- `engine/platform/gamecube/` contains GameCube-specific implementations.
- `scripts/waifulib/xcompile.py` configures the devkitPPC toolchain.
- `wscript` and component `wscript` files select target sources.
- `scripts/build-gamecube.sh` is the canonical local GameCube build command.
- `engine/platform/psvita/` and `engine/platform/nswitch/` are useful console
  precedents, but their APIs must not be copied blindly.

Prefer extending an existing abstraction over adding a parallel build or
platform framework. Keep GameCube-only includes behind `XASH_GAMECUBE` guards.
