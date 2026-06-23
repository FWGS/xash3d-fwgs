# GameCube Local Examples

Use these local patterns before inventing a new platform path:

- `engine/platform/gamecube/sys_gamecube.c`: launch arguments, early platform
  setup, GameCube-specific startup behavior, and diagnostics.
- `engine/platform/gamecube/vid_gamecube.c`: video initialization and bounded
  visible diagnostic output.
- `engine/platform/gamecube/in_gamecube.c`: controller polling through libogc
  PAD APIs and engine input abstractions.
- `engine/platform/gamecube/snddma_gamecube.c`: stable null-audio fallback.
- `engine/platform/gamecube/dll_gamecube.c`: static module/export routing.
- `scripts/build-gamecube.sh`: canonical local GameCube build.
- `scripts/build-gamecube-disc.py`: legal local asset staging into a generated
  GameCube image.
- `scripts/dolphin-boot-probe.sh`: bounded runtime evidence collection.

Console precedents:

- `engine/platform/nswitch/` and `engine/platform/psvita/` may show useful
  project patterns, but their platform APIs and memory models must not be
  copied blindly.
