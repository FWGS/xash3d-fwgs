# Xash3D GameCube Port Plan

## Current status

The repository has a devkitPPC/libogc Waf target, GameCube platform sources,
GX renderer sources, static client/server stubs, and an end-to-end build script
at `scripts/build-gamecube.sh`. A PowerPC ELF and `OUT/bin/boot.dol` have been
produced locally. That proves a link and conversion step completed; it does not
prove the program boots or runs correctly.

The GameCube mappings in `3rdparty/library_suffix` are committed locally as
`663a601`, and parent commit `0f5cf35f` records the updated submodule pointer.
The submodule is clean. The submodule commit must still be pushed to an
accessible remote before a fresh clone outside this workspace can fetch it.

The local AI harness now has a verifier/review gate. Verification labels its
GameCube toolchain, compile/link, and artifact probes separately. Review
rejects patches over 400 changed lines, deleted files, and patches that do not
update this port plan before another autonomous pass is accepted.
The proprietary-SDK scan spells its regex with character classes so the gate
does not reject its own source text.
The harness also has a PyQt6 control panel at
`scripts/xash3d-gc-aider-gui.sh` for supervised one-pass or bounded-loop Aider
runs, verification, DOL/disc builds, and launching the generated ISO in
Dolphin. It uses the existing shell gates rather than duplicating them.
Automation objectives now live in `.ai/goals/GAMECUBE_PORT_GOALS.md`. The
goal runner selects the first incomplete non-manual objective, supplies Aider
with its acceptance criteria and live Git context, and repeats verified,
reviewed patches until the objectives or a configured pass limit are reached.
The Qt console now presents this as a GameCube-inspired devkit pipeline with
live DOL, ISO, and Dolphin status chips. Booting with no ISO automatically
builds the disc first instead of failing with a missing-file dialog. Model,
endpoint, and credential controls are intentionally absent from the GUI.
Mission Control now shows every automatic/manual goal and the active goal's
acceptance criteria. Port Telemetry reports Git cleanliness and tracking,
latest commit, submodule divergence, devkitPPC, game content, inherited Aider
authentication, the latest recorded blocker, and live artifact pipeline state.
The port GUI loads `FOT-Rodin Pro DB.otf` as its application font, restricts
`GameCube.ttf` to the `GameCube` word in the window header, and renders the
provided `GameCube.svg` artwork at top left. These assets are GUI-only and are
not linked into the engine or generated DOL/disc image. Rodin also covers the
telemetry and log panes; neither retains a monospace override.
After an autonomous G01 run stalled asking for files already in the checkout,
the goal runner now preloads a small per-goal source set into Aider. G01 starts
with `engine/server/sv_game.c` (the actual `SV_InitEdict` definition) and
`engine/server/server.h`; later goals receive their platform/backend entry
points while retaining the repository map for related symbols.
The port plan is no longer listed as global read-only Aider context. Every goal
pass now supplies both this plan and the goal ledger as explicit editable files,
alongside its focused source set. Project rules, decisions, blockers, and the
engine porting guide remain read-only. This makes the required documentation
and completion-marker updates possible without broadening source scope.
The GUI defaults goal automation to three passes for supervised proving runs;
the operator can raise the safety limit after stable passes.
After the first G01 attempt produced plans but no edit, the harness switched
from the same-model architect/editor-whole handoff to direct diff editing.
Operational Aider or verifier failures now stay in ignored logs/state rather
than dirtying tracked `BLOCKERS.md`, and an operator stop records `stopped`
instead of leaving the dashboard in a stale `running` state.
An otherwise successful Aider invocation that makes no edit now returns a
distinct status and receives one bounded retry; uncommitted edits still stop
immediately for human review. Repo-map context is capped at 2,048 tokens to
reduce distraction while explicit per-goal files carry the working context.
Goal passes are fully non-interactive: the model must choose the smallest safe,
reversible option from available evidence and may not pause for questions or
approval. Aider no longer creates commits or commit messages. After an edit,
the harness validates the staged patch and commits it with a deterministic,
single-line subject selected by goal; review rejects multiline, unconventional,
or deliberation-contaminated messages.
The Dolphin probe is executable, supports native and Flatpak Dolphin installs,
uses bounded TERM/KILL timeouts, preserves stdout/stderr and internal logs, and
returns distinct statuses for host failure, guest failure, observed bootstrap,
and inconclusive timeout/exit.
The hardened full-build gate caught an invalid `rserr_nomem` result introduced
by the earlier GX buffer patch. GameCube buffer-allocation failure now returns
the existing `rserr_unknown` value from the platform contract; no new enum or
cross-platform ABI change was introduced.
The next GX pass exposed two further automation weaknesses: it used the
nonexistent `OSReport` name instead of libogc's existing `SYS_Report` API, and
the old workflow committed before building. Video diagnostics now use
`SYS_Report`. Autonomous edits are staged and fully built before commit; one
failed build is returned to Qwen with the exact verifier tail for a bounded
non-interactive repair, and only a verified patch is committed. Thinking output
is disabled for direct edit passes to reduce repetitive analysis loops.

## Milestones

1. Reproducible clean build from a fresh checkout.
2. Boot in Dolphin with visible diagnostics and a controlled failure path.
3. Initialize video and controller input.
4. Mount SD/FAT storage and find `xash3d/valve`.
5. Reach the engine console or menu within the memory budget.
6. Load assets and a small map.
7. Validate on physical GameCube hardware.

## Strategy

- Keep platform work in `engine/platform/gamecube/` and use existing backend
  selectors in `common/`.
- Use Waf's `--gamecube` cross-compilation path and libogc rather than a second
  build system.
- Keep game code and renderer modules statically linked.
- Bring up GX output incrementally; do not assume desktop OpenGL behavior.
- Start with SD/FAT storage. Treat optical-disc support as a later milestone.
- Audit large fixed allocations and caches against the 24 MiB main-memory
  limit. ARAM is not a transparent substitute for main memory.
- Keep networking and audio stubbed only where their unsupported state is
  explicit and the engine handles the absence safely.

## Commands and evidence

Previously observed build command:

```sh
scripts/build-gamecube.sh
```

The existing `build/config.log` shows devkitPPC compilation with
`-D__GAMECUBE__` and `-DXASH_GAMECUBE=1`. The generated artifacts are not yet
runtime-tested.

On 2026-06-20, the harness ran:

```sh
scripts/ai-verify.sh
```

Result: Waf configured the GameCube target, compiled and linked
`build/engine/xash`, installed the static libraries and data under `OUT/`, and
generated non-empty `OUT/bin/xash` and `OUT/bin/boot.dol` artifacts. This
verifies the current local source tree builds; it does not verify runtime
behavior.

The GameCube launcher now calls `GCube_EarlyInit()` before `Host_Main`. It
routes stdout and stderr to libogc's Dolphin OSReport channel and emits
`Xash3D GameCube: bootstrap`, so startup logs and fatal errors are observable
before GX video initialization. A local runtime probe was not possible because
no Dolphin executable is installed in this environment.

Verification commands:

```sh
command -v dolphin-emu || command -v dolphin
scripts/ai-verify.sh
strings OUT/bin/boot.dol | grep 'Xash3D GameCube: bootstrap'
```

Result: no Dolphin executable was found; the complete GameCube build passed;
and the bootstrap marker is present in the generated DOL. Disassembly also
confirms `main` calls `GCube_EarlyInit` before `GCube_GetArgv` and `Host_Main`.
A historical note claimed an `SV_InitEdict` `-Wstringop-overflow` warning, but
no captured build log contains that compiler diagnostic. On 2026-06-21,
`scripts/build-gamecube.sh` completed successfully and a search of its full log
found no `SV_InitEdict`, `stringop-overflow`, or compiler warning. The audited
layout has a direct, fixed-size `entvars_t v` member in `edict_t`, and the
existing `memset` uses exactly `sizeof( entvars_t )`. G01 is therefore closed
without changing ABI-sensitive layout or adding an unjustified suppression.

On 2026-06-20, `OUT/bin/boot.dol` was run in Dolphin 2603a (Flatpak) in batch
mode with OSReport enabled. The first run reached engine initialization and
failed because the statically built `filesystem_stdio` archive was not linked
and registered with the GameCube module loader:

```text
Xash3D GameCube: bootstrap
Warning: SD card init failed, using DVD paths only
FS_LoadProgs: can't load filesystem library filesystem_stdio.so
```

The GameCube build now links `filesystem_stdio`, registers its `GetFSAPI` and
`CreateInterface` exports, and prefixes its ten engine-colliding implementation
symbols with `GCFS_`. The full build passed and a second Dolphin run reported:

```text
FS_LoadProgs: filesystem_stdio successfully loaded
Changing directory to  failed: No such device
```

This confirms the DOL boots and the filesystem module initializes. The current
Dolphin test profile has no SD Gecko/FAT volume, so `fatInitDefault()` fails and
the fallback root path collapses to an empty path. Dolphin logs are captured in
the ignored `.ai/logs/` directory.

The local `Half-Life/valve` tree contains 540 MiB of game data. It is too large
to embed in the executable under the GameCube's 24 MiB RAM limit. The engine
now mounts a native GameCube DVD through libogc's `__io_gcdvd` and ISO9660
interfaces after trying SD Gecko. `scripts/build-gamecube-disc.py` creates a
bootable GameCube image containing the DOL, a minimal open-source apploader,
an FST, `xash3d/valve`, and the built `extras.pk3`:

```sh
scripts/build-gamecube-disc.py --output OUT/xash3d-gc.iso
```

The generated 561 MiB test image has valid GameCube magic, loads both DOL
sections and its FST, and is recognized by Dolphin as `GXHE00` (NTSC-U).
`dolphin-tool extract --list` confirms the required `liblist.gam`, `gfx.wad`,
and `extras.pk3` paths. This is a native disc/FST path with no Dolphin-host
filesystem dependency; the local game assets and generated image remain
ignored and must never be committed or redistributed.

Dolphin 2603a Flatpak currently traps in its host CPU-GPU thread at a fixed
`ud2` after the apploader handoff, with Null, Software, and OpenGL backends.
This occurs before the guest bootstrap marker. Direct DOL boot still reaches
OSReport, so the next validation should use a different Dolphin build or a
homebrew-capable physical GameCube.

## Next blocker

Verify GX video initialization and diagnostic frame presentation in Dolphin.
`R_Init_Video` now explicitly allocates the software buffer during GX init
(rather than deferring to `R_ChangeDisplaySettings`), and `GC_PresentBuffer`
falls back to a solid blue diagnostic frame when the software buffer is not
yet ready. This ensures a visible frame appears even before the software
renderer draws content. Boot `OUT/xash3d-gc.iso` in Dolphin and confirm
that the screen shows a blue diagnostic frame or engine-rendered content
rather than remaining blank or trapping. Capture OSReport and video output.
Also publish the `gamecube-platform` submodule branch to an accessible remote
so fresh clones can fetch the recorded commit.

Changes in this pass:
- `R_Init_Video` calls `SW_CreateBuffer` immediately after `GC_InitVideoHardware`
  so the buffer exists before the first frame.
- `GC_PresentBuffer` renders a solid blue (RGB565 `0x001F`) diagnostic frame
  when `gc.buffer` is NULL, providing visible evidence of GX output.
- OSReport messages added for buffer allocation success/failure.

The repository now includes `scripts/dolphin-boot-probe.sh`, which builds the
disc image, launches a bounded Dolphin boot probe, captures logs, and
distinguishes emulator-host failures from guest-engine failures. Logs are
preserved under `.ai/logs/dolphin-probe-<timestamp>/`.

Probe command:
```sh
scripts/dolphin-boot-probe.sh
```

Expected output distinguishes:
- `PASS`: Guest bootstrap marker found in logs.
- `FAIL`: Emulator-host crash or missing dependency.
- `TIMEOUT`: Guest may be running or hung.
- `INCONCLUSIVE`: Clean exit without guest marker.

## Next wake-up commands

```sh
git status --short
git -C 3rdparty/library_suffix diff --check
scripts/ai-verify.sh
scripts/dolphin-boot-probe.sh
```
