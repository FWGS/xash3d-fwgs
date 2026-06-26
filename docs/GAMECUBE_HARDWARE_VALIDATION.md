# GameCube Hardware Validation

This is the repeatable test protocol for goals that automation cannot complete
inside Dolphin alone. Dolphin probes are useful for build and bootstrap sanity,
but final native acceptance requires real console or hardware-equivalent loader
validation with comparable evidence, a clear pass/fail status, and a next
blocker.

## Purpose

Use this document to validate that the GameCube port works on physical hardware
or a hardware-equivalent loader path, and to record evidence for goals that
cannot be completed through automated Dolphin-only tests.

This is especially important for:

- Boot behavior.
- Controller behavior.
- Storage write/read behavior.
- Audio backend behavior.
- Memory pressure.
- Frame pacing.
- Loader compatibility.
- Failure cases that Dolphin may hide.

## Required Setup

- A homebrew-capable GameCube or compatible Wii/GameCube loader path.
- One supported controller in port 0.
- A known-good video output path and capture method.
- The generated `OUT/bin/boot.dol` or `OUT/xash3d-gc.iso`.
- The same legal local `Half-Life/valve` asset set used by the Dolphin probes.
- A writable route if testing saves/configs: SD Gecko, memory-card adapter,
  Wii SD route, or the chosen storage backend from the port plan.
- A way to capture evidence: photo, video, serial log, loader log, screen
  capture, or handwritten notes copied into the port plan.

## Hardware Handoff Packet

Before a physical-console run, generate a handoff packet:

```sh
scripts/gamecube-hardware-handoff.sh --build --build-disc
```

Use `scripts/gamecube-hardware-handoff.sh` without options when current build
artifacts already exist and only a fresh manifest/checklist is needed. The
packet is written under `.ai/logs/hardware-handoff-*` and contains an artifact
manifest, an operator checklist, and an evidence template. It does not copy or
package proprietary Half-Life assets, Nintendo SDK files, BIOS/IPL dumps, or
proprietary Nintendo documentation.

## Boot Media Failure Preflight

Before recording physical loader results, run the automated G43 preflight:

```sh
scripts/gamecube-boot-media-compliance.py --build-disc
```

This verifies artifact hashes, smoke staging, missing-file rejection,
case-mismatch rejection, and corrupt ISO/GCM diagnostics. Treat its log
directory as local preflight evidence only. Swiss, ODE, or physical-console
loader failures still need dated operator evidence in the matrix below,
including the artifact checksum, loader version, launch route, visible error,
and whether the result was loader failure, bootstrap failure, engine failure, or
a known bounded timeout.

## Validation Matrix

Record which route was tested.

The support contract for these routes lives in
`docs/GAMECUBE_HARDWARE_MATRIX.md`. This protocol records evidence; the matrix
defines which routes are required, recommended, diagnostic, or unsupported.

| ID | Hardware | Loader | Artifact | Storage | Required |
| --- | --- | --- | --- | --- | --- |
| HW-GC-DOL-SD | GameCube | Swiss / homebrew loader | `boot.dol` | SD writable | Yes |
| HW-GC-ISO-RO | GameCube | Disc / ISO loader | `xash3d-gc.iso` | read-only disc | Yes |
| HW-WII-DOL-SD | Wii in GameCube mode / compatible loader | homebrew loader | `boot.dol` | SD writable | Recommended |
| HW-DOLPHIN-SD | Dolphin with SD-backed profile | Dolphin | `boot.dol` or ISO | SD writable | Useful but not final |
| HW-DOLPHIN-RO | Dolphin disc-only profile | Dolphin | ISO | read-only | Useful but not final |

A goal may use Dolphin evidence for smoke validation, but hardware-complete
status requires at least one real hardware route.

## Record For Each Run

Every hardware run should record:

- Date and tester.
- Hardware revision: GameCube model/region, Wii model if applicable, and video
  output path.
- Loader: Swiss version, homebrew loader version, Wii loader version, and launch
  method.
- Artifact: commit hash, build command, artifact path, and checksum if
  available.
- Asset route: asset source path, `valve` folder location, and whether assets
  are on SD, disc, or another route.
- Storage route: no writable storage, SD Gecko, Wii SD, memory-card adapter, or
  other backend.
- Boot result: no signal, loader failure, bootstrap log, engine readiness
  marker, menu, map, or gameplay.
- Controller result: detected controller, movement, look, use, fire, jump,
  pause/menu, and console/menu navigation if enabled.
- Storage result: first boot, second boot, config write, config reload, save
  write, save load, missing storage behavior, and corrupted config behavior.
- Audio result: null fallback, effects, ambient, music, and no unbounded hang
  when audio is unavailable.
- Stability result: clean exit/reset, bounded hang, crash, thermal issue, power
  issue, or loader exception.
- Performance notes: approximate frame pacing, stalls, memory pressure symptoms,
  asset loading delays, and input latency.

## Minimum Completion Bar

The native port is not hardware-complete until a real console can:

- Boot the selected artifact.
- Reach the engine readiness marker.
- Load a legal Half-Life map.
- Spawn the player.
- Accept controller input.
- Run long enough to observe frame pacing and memory behavior.
- Exercise the current audio policy without an unbounded hang.
- Exercise the current storage policy without writing generated state to
  read-only disc paths.

## Required Test Cases

### HW-BOOT-001: Artifact Boots

Expected result:

- Loader accepts the artifact.
- Display initializes.
- The port reaches the earliest visible boot marker.
- Failure identifies whether the issue is loader, video, bootstrap, or engine.

Evidence:

- Photo/video of loader and boot result.
- Log if available.

### HW-BOOT-002: Engine Readiness Marker

Expected result:

- Engine reaches the documented readiness marker.
- No unbounded hang before subsystem initialization completes.

Evidence:

- Log line, capture, or photo/video of final visible state.

### HW-FS-001: Disc-Only Read-Only Boot

Expected result:

- The port can use `gcdisc:/xash3d` as read-only content.
- The engine does not attempt generated writes to `gcdisc:/`.
- If writable storage is missing, config/save writes are skipped with a
  diagnostic.

Evidence:

- Log showing read-only fallback.
- No ISO9660 write error.
- Boot reaches readiness marker or a later known blocker.

### HW-FS-002: Writable SD First Boot

Expected result:

- The port detects the writable storage route.
- The port creates required directories if missing.
- Expected layout exists after boot: `sd:/xash3d`, `sd:/xash3d/valve`,
  `sd:/xash3d/valve/save`, `sd:/xash3d/valve/logs`, and
  `sd:/xash3d/valve/screenshots`.

Evidence:

- Photo/log of boot.
- Post-run directory listing or photo.

### HW-FS-003: Writable SD Second Boot

Expected result:

- The second boot reuses the existing writable layout.
- No duplicate, corrupt, or conflicting config state is created.
- Existing config is read safely.

Evidence:

- First and second boot notes.
- Log comparison if available.

### HW-FS-004: Config Write And Reload

Expected result:

- A config write succeeds when writable storage exists.
- A second boot observes the persisted config.
- A missing writable route skips the config write safely.

Evidence:

- Command used.
- Config file timestamp or content proof.
- Second boot result.

### HW-FS-005: Corrupted Config Recovery

Expected result:

- A malformed config does not create an unbounded hang.
- The engine either ignores the bad config, reports a diagnostic, or falls back
  to defaults.
- The next concrete blocker is recorded if recovery fails.

Evidence:

- Corrupted file used.
- Boot result.
- Diagnostic or failure mode.

### HW-MAP-001: Map Load

Expected result:

- A legal Half-Life map loads from the selected asset route.
- Failure is classified as missing asset, bad path, filesystem mount issue,
  memory issue, BSP parsing issue, entity spawn issue, or renderer issue.

Evidence:

- Selected map name.
- Visible result or log.
- First failing marker.

### HW-PLAYER-001: Player Spawn

Expected result:

- Player spawns after map load.
- Camera/view initializes.
- No immediate crash after entity spawn.

Evidence:

- Capture/photo of spawned player view.
- Log marker if available.

### HW-INPUT-001: Controller Port 0

Expected result:

- Controller in port 0 is detected.
- Movement, look, use, fire, jump, and menu/pause inputs respond.
- Disconnect/reconnect behavior is recorded if tested.

Evidence:

- Video clip or tester notes.

### HW-AUDIO-001: Audio Policy

Expected result:

- Current audio backend behavior matches the port plan.
- If audio is disabled/null, the engine continues without an unbounded hang.
- If audio is implemented, effects and ambient/music behavior are recorded.

Evidence:

- Capture/video.
- Notes on audible output or null fallback.

### HW-STABILITY-001: Short Soak

Expected result:

- The port runs for at least 5 minutes after reaching the furthest available
  gameplay state.
- No unbounded hang, thermal failure, or power instability occurs.
- If the port crashes, the crash is classified and the next blocker is recorded.

Evidence:

- Start/end timestamps.
- Video or notes.
- Crash marker if applicable.

## Failure Taxonomy

Use one of these labels when a run fails:

- `loader_failure`
- `no_video`
- `bootstrap_failure`
- `filesystem_mount_failure`
- `asset_lookup_failure`
- `config_write_failure`
- `save_load_failure`
- `bsp_load_failure`
- `entity_spawn_failure`
- `renderer_failure`
- `controller_failure`
- `audio_failure`
- `memory_pressure`
- `performance_stall`
- `bounded_hang`
- `unbounded_hang`
- `crash`
- `thermal_or_power_issue`
- `unknown`

Every failure must include the next concrete blocker.

Bad:

```text
Does not work on hardware.
```

Good:

```text
HW-MAP-001 failed: asset_lookup_failure. Boot reaches engine readiness marker,
but map load fails after resolving `valve/maps/c0a0.bsp`. Next blocker is to
trace GameCube search path resolution between `gcdisc:/xash3d/valve` and
`sd:/xash3d/valve`.
```

## Evidence Format

Add a dated entry to `docs/GAMECUBE_PORT_PLAN.md` with:

- Test ID.
- Date.
- Tester.
- Hardware route.
- Loader route.
- Storage route.
- Artifact path.
- Commit hash.
- Build command.
- Asset route.
- Result: pass, partial, or fail.
- Furthest reached marker.
- Evidence reference.
- Failure taxonomy label if failed.
- Next concrete blocker.

Keep photos, videos, and logs outside Git if they contain proprietary assets.

## Evidence Entry Template

```md
### Hardware validation — YYYY-MM-DD — TEST-ID

- Tester:
- Commit:
- Build command:
- Artifact:
- Hardware:
- Loader:
- Video route:
- Storage route:
- Asset route:
- Result:
- Furthest reached:
- Evidence:
- Failure label:
- Notes:
- Next blocker:
```

## Completion Rule

A hardware validation goal may be marked complete only when:

- The required test cases have dated evidence.
- The artifact commit is recorded.
- The hardware and loader route are recorded.
- The furthest reached marker is clear.
- Any failed test has a concrete next blocker.
- Manual-only criteria are explicitly linked to the hardware validation goal
  instead of being retried by automation.

Do not mark hardware validation complete from Dolphin-only evidence. Separate
`source-complete`, `Dolphin-smoke-complete`, and `hardware-complete` in all goal
and port-plan updates.
