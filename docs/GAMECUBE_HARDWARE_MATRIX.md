# GameCube Hardware And Loader Matrix

Status: G39 complete as a support-policy document. Runtime proof still belongs
to G38 and the later hardware release gates.

This matrix defines the minimum supported hardware, loader, artifact, storage,
video, controller, and region combinations for the native GameCube port. It is
not a claim that every route has already passed on real hardware; each route
must still carry evidence in `docs/GAMECUBE_PORT_PLAN.md` before release.

## Support Levels

| Level | Meaning |
| --- | --- |
| Required | Must work before a native GameCube release candidate can be accepted. |
| Recommended | Should work before release, but may ship with a documented limitation if a required route is solid. |
| Diagnostic | Useful for development evidence, not accepted as final hardware proof. |
| Unsupported | Do not spend goal-runner time trying to make this route work unless the support policy changes. |

## Minimum Release Matrix

| Route ID | Level | Hardware | Loader | Artifact | Content route | Writable route | Video | Controller | Region | Evidence owner |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| GC-DOL-SD | Required | Real GameCube DOL-001 or DOL-101 | Swiss or equivalent homebrew DOL loader | `OUT/bin/boot.dol` | `sd:/xash3d/valve` | `sd:/xash3d` | 480i NTSC or PAL matching console | Official wired controller in port 0 | NTSC-U first, PAL/J later | G38/G53/G66 |
| GC-ISO-RO | Required | Real GameCube DOL-001 or DOL-101 | Swiss, ODE, or compatible disc-image loader | `OUT/xash3d-gc.iso` | `gcdisc:/xash3d/valve` | None, or optional SD if mounted | 480i NTSC or PAL matching console | Official wired controller in port 0 | NTSC-U first, PAL/J later | G38/G53/G66 |
| WII-GC-DOL-SD | Recommended | Wii running GameCube-mode compatible loader | Homebrew/Swiss/Nintendont-style DOL route only if behavior matches GC constraints | `OUT/bin/boot.dol` | SD `xash3d/valve` tree | SD `xash3d` tree | 480i or loader-provided safe mode | Official wired controller or compatible adapter path | NTSC-U/PAL | G53/G66 |
| DOLPHIN-DOL-SD | Diagnostic | Dolphin | DOL boot probe | `OUT/bin/boot.dol` | SD-backed test profile or host-staged content | SD-backed test profile | 4:3 GameCube video mode | Emulated port 0 controller | Any | G36/G40/G41 |
| DOLPHIN-ISO-RO | Diagnostic | Dolphin | Disc image boot | `OUT/xash3d-gc.iso` | ISO9660/GameCube FST | None unless SD test profile is mounted | 4:3 GameCube video mode | Emulated port 0 controller | Any | G36/G40/G41 |

## Artifact Commands

Build the DOL route:

```sh
scripts/build-gamecube.sh
```

Build the local legal smoke ISO route:

```sh
scripts/build-gamecube-disc.py --smoke-map c0a0e
```

Generate a real-hardware handoff packet with hashes and operator checklist:

```sh
scripts/gamecube-hardware-handoff.sh --build --build-disc
```

Run the release-candidate evidence gate:

```sh
scripts/gamecube-rc-check.sh
```

Run a campaign compatibility audit:

```sh
scripts/gamecube-campaign-audit.sh
```

## SD Layout Contract

The writable SD route uses this layout:

```text
sd:/apps/xash3d-gc/boot.dol
sd:/xash3d/valve/
sd:/xash3d/valve/save/
sd:/xash3d/valve/logs/
sd:/xash3d/valve/screenshots/
```

The disc route is read-only. If writable SD storage is unavailable, config,
save, log, and screenshot writes must be skipped or redirected with a readable
diagnostic. The engine must not attempt generated writes into `gcdisc:/`.

## Video Policy

The minimum supported output is the console's standard 4:3 480i-compatible
mode. Do not require 480p, progressive-scan cables, widescreen hacks, Dolphin
enhancements, or nonstandard internal resolution behavior. UI and fatal
diagnostic text must remain inside a conservative CRT-safe area.

## Controller Policy

The minimum supported controller is an official wired GameCube controller in
port 0. WaveBird, third-party controllers, disconnect/reconnect behavior, and
no-controller boot are release-quality follow-up gates, not prerequisites for
G39. They must be recorded separately before final release.

## Region Policy

NTSC-U is the first required validation region because it is the current primary
development path. PAL and NTSC-J should be tested before a broad release. Region
failures must be recorded as explicit limitations instead of being folded into
generic hardware failures.

## Explicitly Unsupported Routes

| Route | Reason |
| --- | --- |
| Proprietary Nintendo SDK builds | The project remains clean-room homebrew/libogc based. |
| BIOS/IPL-dependent behavior | Release and tests must not require BIOS/IPL dumps. |
| Writes to `gcdisc:/` or generated state beside disc content | The disc image route is read-only. |
| Host absolute asset paths | Hardware cannot reproduce developer workstation paths. |
| Dolphin enhancements as requirements | Emulator-only timing, memory, graphics, or filesystem behavior is not hardware proof. |
| 16:9-only, 480p-only, or high-internal-resolution-only modes | The minimum console route is 4:3 480i-compatible output. |
| Keyboard/mouse-only gameplay | The release route must be playable with GameCube controller input. |
| Bundled Half-Life assets or Nintendo proprietary files in Git/release archives | Legal/compliance policy forbids distributing those assets without rights. |

## Completion Rule

G39 is complete when this support matrix is present and linked from the port
plan, hardware validation protocol, and goal ledger. It does not close G38 or
any later hardware evidence gate. If a future run changes supported routes, it
must update this document and record why the support contract changed.

## Evidence Matrix (G53)

This section is the release evidence ledger for hardware and loader validation.
Each row must be updated from a dated Dolphin probe, RC gate, or operator-run
hardware session. Dolphin rows are diagnostic only; real hardware rows remain
open until an operator records physical console evidence.

| Test ID | Artifact commit | Loader | Storage route | Video mode | Controller | Boot result | Map result | Audio result | Save result | Memory card | Next blocker |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| DOLPHIN-DOL-SD-001 | `23889d4f` or newer local build | Dolphin DOL boot probe | SD-backed test profile | 4:3 GameCube mode | Emulated official port 0 | PASS, engine readiness marker observed in probes | PASS, `c0a0e` reaches `MAP_READY` in probes | NOT_TESTED, audible output still needs operator evidence | NOT_TESTED, SD round trip remains G58/G66 | NOT_TESTED | Audible audio, save/config round trip, and real hardware evidence |
| GC-SWISS-SD2SP2-001 | pending hardware run | Swiss / SD2SP2 | `sd:/xash3d` writable | NTSC/PAL 480i | Official wired controller | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | Slot A and Slot B NOT_TESTED | Real GameCube + Swiss SD2SP2 operator run |
| GC-SWISS-SDGECKO-001 | pending hardware run | Swiss / SD Gecko | `sd:/xash3d` writable | NTSC/PAL 480i | Official wired controller | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | Slot A and Slot B NOT_TESTED | Real GameCube + Swiss SD Gecko operator run |
| GC-DISC-RO-001 | pending hardware run | Swiss/ODE/native disc image | `gcdisc:/xash3d` read-only | NTSC/PAL 480i | Official wired controller | NOT_TESTED | NOT_TESTED | NOT_TESTED | Read-only route expected to skip writes | NOT_TESTED | Physical disc or disc-image loader run |
| WII-GC-MODE-001 | pending hardware run | Wii GameCube mode | SD or disc | NTSC/PAL safe mode | Official or WaveBird | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | Wii/GameCube-mode operator run |
| CTRL-WAVEBIRD-001 | pending hardware run | Swiss or equivalent | SD writable | NTSC/PAL 480i | WaveBird | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | WaveBird input evidence |
| CTRL-THIRDPARTY-001 | pending hardware run | Swiss or equivalent | SD writable | NTSC/PAL 480i | third-party controller | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | Third-party controller evidence |
| CTRL-NOCONTROLLER-001 | pending hardware run | Swiss or equivalent | SD writable | NTSC/PAL 480i | no-controller boot | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | No-controller boot and reconnect evidence |
| CTRL-DISCONNECT-001 | pending hardware run | Swiss or equivalent | SD writable | NTSC/PAL 480i | mid-game disconnect | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | NOT_TESTED | Mid-game disconnect/reconnect evidence |

Required entry fields are: Test ID, Artifact commit, Loader, Storage route,
Video mode, Controller, Boot result, Map result, Audio result, Save result,
Memory card, and Next blocker. Proprietary local asset captures, screenshots,
videos, and audio recordings must stay outside Git; only textual log paths,
hashes, route IDs, and summary markers belong in this matrix.

Dolphin evidence is diagnostic only and is not accepted as final hardware proof.
Real hardware evidence remains under G38/G66 until a dated operator run records
the required route, loader, storage, controller, memory-card, audio, save, and
shutdown behavior.

## Pure Flipper GX (G198)

Retail GameCube builds ship a Flipper-only renderer (`ref_gx`). Soft edge/span
rasterization is not used for gameplay presents. Soft DumpFrames latch, ViSwap
throttles, wall-aim pumps, and long VSync drains are confined to
`GC_IsCaptureDiagnostics()` (`-gcdumpframes` / `-gcdump` / `-gcchangelevel` /
`-gcnewgame` / `-gcmap` / `-gcworldrender`). `-gcsoftworld` is rejected.

Retail boots do not require probe argv or `gamecube.cfg` tokens. Disc overrides
may still inject `-gcnewgame` / `-gcworldrender` for automated probes. Present
contract: `GX_DrawDone` → `GX_CopyDisp` → `VIDEO_SetNextFramebuffer` →
`VIDEO_Flush` → field-safe `VIDEO_WaitVSync` → dual-XFB rotate. XFB policy:
CPU writes through cached K0 + `DCFlushRange`; VI/`GX_CopyDisp` use K1.
480i CRT keeps VI vertical filter on CopyDisp; progressive uses a lean copy.
Soft/internal RGB565 stays ≤320×240 (BSS) for MEM1 tip safety while Flipper
EFB/XFB remain native console resolution from `VIDEO_GetPreferredMode`.
Host play target is 60 fields/sec (G284); VI paces presents.

Hardware validation still requires the GC-DOL-SD and GC-ISO-RO routes above
(480i, wired controller, audio, map transitions, save behavior, fatal display,
sustained frame pacing). Latest repository handoff packet:

`.ai/logs/hardware-handoff-20260723-145125`
