#!/usr/bin/env bash
# Whole-script timeout wrapper with enough headroom for rebuild/disc/log work.
if [ "${GC_BOOT_PROBE_INNER:-0}" != "1" ]; then
	if [ -n "${GC_BOOT_PROBE_TIMEOUT:-}" ]; then
		PROBE_TIMEOUT="$GC_BOOT_PROBE_TIMEOUT"
	else
		EMU_TIMEOUT="${DOLPHIN_TIMEOUT:-180}"
		PROBE_TIMEOUT=$(( EMU_TIMEOUT + 300 ))
		if [ "$PROBE_TIMEOUT" -lt 420 ]; then
			PROBE_TIMEOUT=420
		fi
	fi
	export GC_BOOT_PROBE_INNER=1
	exec timeout --foreground --signal=TERM --kill-after=10 "$PROBE_TIMEOUT" "$0" "$@"
fi

cleanup_boot_probe_processes() {
    # Only clean the probe/emulator family for this repo.
    # Do not include the ISO argument in the pattern: it is also present in
    # this shell's command line and can make pkill terminate the probe itself.
    pkill -TERM -f '[d]olphin-emu|[D]olphinQt' 2>/dev/null || true
    sleep 1
    pkill -KILL -f '[d]olphin-emu|[D]olphinQt' 2>/dev/null || true
}

cleanup_boot_probe_locks() {
    rm -f .ai/dolphin-probe.lock .ai/goal-supervisor.lock .ai/goal-loop.lock 2>/dev/null || true
}

on_boot_probe_exit() {
    rc=$?
    cleanup_boot_probe_processes
    cleanup_boot_probe_locks
    exit "$rc"
}

trap on_boot_probe_exit EXIT INT TERM

set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
if [[ -f scripts/gamecube-env.sh ]]; then
	source scripts/gamecube-env.sh
fi

# shellcheck source=scripts/dolphin-probe-lock.sh
source scripts/dolphin-probe-lock.sh || exit $?
# shellcheck source=scripts/dolphin-probe-common.sh
source scripts/dolphin-probe-common.sh || exit $?

LOG_DIR=".ai/logs/dolphin-probe-$(date +%Y%m%d-%H%M%S)"
ISO_PATH="$ROOT/$LOG_DIR/xash3d-gc.iso"
PREBUILT_ISO="${1:-${DOLPHIN_PREBUILT_ISO:-}}"
if [[ -n "$PREBUILT_ISO" && "$PREBUILT_ISO" != /* ]]; then
	PREBUILT_ISO="$ROOT/$PREBUILT_ISO"
fi
USER_DIR="$ROOT/$LOG_DIR/dolphin-user"
DOLPHIN_RETAIL="${DOLPHIN_RETAIL:-0}"
DOLPHIN_NEWGAME="${DOLPHIN_NEWGAME:-0}"
# G471: Default to smoke-map mode for bounded testing. Even when DOLPHIN_NEWGAME=1
# is set by automation, use smoke-map unless DOLPHIN_RETAIL=1 is explicitly set.
# This prevents full retail disc builds (~500MB) that timeout on GameCube boot.
if (( DOLPHIN_NEWGAME )); then
	# Only enable retail mode if DOLPHIN_RETAIL=1 is explicitly set
	if [[ "$DOLPHIN_RETAIL" != "1" ]]; then
		# Use smoke-map mode with newgame args, not full retail disc
		DOLPHIN_RETAIL=0
	fi
	DOLPHIN_SKIP_INTRO=1
fi
if [[ "${DOLPHIN_MENU_NEWGAME:-0}" == "1" ]]; then
	GUEST_ARGS+=("-gcmenuplaystart")
	SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0}"
	MAP_MARKER="Xash3D GameCube: map loaded ${SMOKE_MAP}"
	PLAY_READY_MARKER="Xash3D GameCube: play start ready ${SMOKE_MAP}"
	echo "==> Fallback-menu New Game probe (expect map ${SMOKE_MAP})"
fi
# G440-G470: Default to smoke-map mode for bounded testing. Retail mode
# builds full discs (~500MB) that timeout on GameCube boot. Use smoke-map
# unless explicitly configured for retail testing.
# Note: DOLPHIN_RETAIL defaults to 0, which enables smoke-map mode.
# When DOLPHIN_RETAIL=1, it builds a full retail disc (no smoke map).
if [[ "$DOLPHIN_RETAIL" == "1" ]]; then
	TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-240}"
else
	# Entity spawn on large smoke maps (c1a0) needs headroom after BSP load.
	# Note: DOLPHIN_NEWGAME alone doesn't trigger retail mode; smoke-map is used
	TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-300}"
fi
FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-8}"
if (( DOLPHIN_NEWGAME )); then
	# Use smoke-map mode even with DOLPHIN_NEWGAME=1 to avoid full retail disc
	SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0}"
	# G278 intro + G280 Flipper FPS: need sustained sample after present marker.
	FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-40}"
	# G280 playable target is 24–30 FPS (not 60).
	export TARGET_FRAME_TIME="${TARGET_FRAME_TIME:-33.33}"
else
	SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0e}"
fi
DOLPHIN_WORLD_RENDER="${DOLPHIN_WORLD_RENDER:-0}"
GC_FATAL_TEST="${GC_FATAL_TEST:-0}"
GC_PHASE_TEST="${GC_PHASE_TEST:-}"
DOLPHIN_CHANGELEVEL="${DOLPHIN_CHANGELEVEL:-}"
GUEST_MARKER="Xash3D GameCube: bootstrap"
READY_MARKER="Xash3D GameCube: engine subsystems ready"
RETAIL_MENU_MARKER="Xash3D GameCube: retail menu steam background ready"
RETAIL_MENU_INTERACTIVE_MARKER="Xash3D GameCube: retail menu button text ready"
RETAIL_MENU_BG_FALLBACK_MARKER="Xash3D GameCube: mainui vidinit background ready"
RETAIL_MENU_READY_FALLBACK_MARKER="Xash3D GameCube: mainui vidinit menu ready"
MENU_ACTION_READY_MARKER="Xash3D GameCube: probe menu input ready"
INTRO_MARKER="Xash3D GameCube: intro AVI decoded first frame"
MAP_MARKER="Xash3D GameCube: map loaded ${SMOKE_MAP}"
PLAY_READY_MARKER="Xash3D GameCube: play start ready ${SMOKE_MAP}"
FRAME_ARMED_MARKER="Xash3D GameCube: frame budget samples armed after map ready"
INPUT_MARKER="Xash3D GameCube: input polling active"
G45_READY_MARKER="Xash3D GameCube: G45 controller ready"
G45_WAIT_MARKER="Xash3D GameCube: G45 controller waiting"
G37_FATAL_MARKER="G37: Intentional fatal error triggered"
G82_FAULT_MARKER=""
if [[ -n "$GC_PHASE_TEST" ]]; then
	G82_FAULT_MARKER="G82: Intentional phase fault at ${GC_PHASE_TEST}"
fi
DOLPHIN_MMU="${DOLPHIN_MMU:-True}"

mkdir -p "$USER_DIR/Config"

DUMP_FRAMES=0

# Retail screenshot capture is useful for visual evidence but changes Dolphin
# host throughput substantially.  Keep the historical retail default while
# allowing an explicit zero for timing/audio validation.
if [[ "${DOLPHIN_DUMP_FRAMES:-}" == "0" ]]; then
	DUMP_FRAMES=0
elif [[ "$DOLPHIN_RETAIL" == "1" || "${DOLPHIN_DUMP_FRAMES:-0}" == "1" ]]; then
	DUMP_FRAMES=1
fi

# Retail timing evidence must use Dolphin's JIT and CPU thread unless a
# caller explicitly selects another profile.  Smoke-map probes retain their
# conservative deterministic defaults; retail capture otherwise measures the
# emulator's logging/render throughput instead of the guest's pacing.
if [[ "$DOLPHIN_RETAIL" == "1" ]]; then
	DOLPHIN_CPU_CORE="${DOLPHIN_CPU_CORE:-1}"
	DOLPHIN_CPU_THREAD="${DOLPHIN_CPU_THREAD:-True}"
else
	DOLPHIN_CPU_CORE="${DOLPHIN_CPU_CORE:-0}"
	DOLPHIN_CPU_THREAD="${DOLPHIN_CPU_THREAD:-False}"
fi

cat > "$USER_DIR/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = ${DOLPHIN_CPU_CORE}
CPUThread = ${DOLPHIN_CPU_THREAD}
DSPHLE = True
FastDiscSpeed = True
MMU = ${DOLPHIN_MMU}
AccurateCPUCache = ${DOLPHIN_MMU}
SIDevice0 = 6
SIDevice1 = 0
SIDevice2 = 0
SIDevice3 = 0
[Interface]
ConfirmStop = False
EOF

if (( DUMP_FRAMES )); then
	cat >> "$USER_DIR/Config/Dolphin.ini" <<'EOF'
[Movie]
DumpFrames = True
DumpFramesSilent = True
EOF
	# G192: DumpFrames freeze was ImmediateXFB (ViSwap skipped) + VRAM XFB
	# stitch. CPU soft blits after CopyDisp never reach DumpFrames when
	# ImmediateXFB is on. Disable both; soft latch relies on ViSwap RAM decode.
	# G194: PNGCompressionLevel=1 — default zlib-6 was too slow (soft latch never
	# reached the dump queue); level 0 wrote ~1MB/frame and I/O-bound the same.
	# SkipDuplicateXFBs helps when XFB cache hits; with DisableCopyToVRAM the
	# local Dolphin patch always misses (G192), so guest stamps soft latch XFBs
	# and rate-limits non-latch SetNextFramebuffer to cut early dump spam.
	cat > "$USER_DIR/Config/GFX.ini" <<'EOF'
[Settings]
DumpFramesAsImages = True
PNGCompressionLevel = 1
[Hacks]
XFBToTextureEnable = False
DisableCopyToVRAM = True
SkipDuplicateXFBs = True
ImmediateXFBEnable = False
EOF
fi

cat > "$USER_DIR/Config/Logger.ini" <<'EOF'
[Logs]
BOOT = True
CORE = True
DVD = False
OSREPORT = True
OSREPORT_HLE = True
PowerPC = False
[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = True
WriteToWindow = False
EOF

echo "==> Building GameCube engine and DOL..."
SKIP_ENGINE=0
SKIP_DISC=0
if [[ "${DOLPHIN_SKIP_BUILD:-0}" == "1" && -f "$PREBUILT_ISO" ]]; then
	SKIP_ENGINE=1
	if (( DOLPHIN_NEWGAME )); then
		SKIP_DISC=0
	elif [[ "$DOLPHIN_RETAIL" == "1" ]] || [[ "${DOLPHIN_REUSE_ISO:-0}" == "1" ]]; then
		SKIP_DISC=1
	fi
fi

if (( SKIP_DISC )); then
	echo "==> Using pre-built ISO (DOLPHIN_SKIP_BUILD=1): $PREBUILT_ISO"
	mkdir -p "$(dirname "$ISO_PATH")"
	rm -f "$ISO_PATH"
	if ! ln "$PREBUILT_ISO" "$ISO_PATH" 2>/dev/null; then
		if ! ln -s "$PREBUILT_ISO" "$ISO_PATH" 2>/dev/null; then
			cp -f "$PREBUILT_ISO" "$ISO_PATH"
		fi
	fi
else
	if (( SKIP_ENGINE )); then
		echo "==> Skipping engine rebuild (DOLPHIN_SKIP_BUILD=1); rebuilding disc image..."
	else
		if ! XASH3D_GC_SKIP_DISC_BUILD=1 bash scripts/build-gamecube.sh; then
			echo "FAIL: Engine build failed."
			exit 1
		fi
	fi

	echo "==> Building GameCube disc image..."
	BUILD_ARGS=(--output "$ISO_PATH")
	if [[ "$DOLPHIN_RETAIL" == "1" ]]; then
		SMOKE_MAP=""
		BUILD_ARGS+=(--data Half-Life/valve)
		echo "==> Retail disc mode (full valve assets, no smoke map)"
		echo "WARNING: Full retail disc mode may timeout on GameCube boot. Use smoke-map mode for bounded testing."
		if [[ "${DOLPHIN_SKIP_INTRO:-0}" == "1" ]]; then
			BUILD_ARGS+=(--skip-startup-vids)
			echo "==> Skipping startup cinematic for faster menu validation"
		fi
		if (( DOLPHIN_NEWGAME )); then
			BUILD_ARGS+=(--probe-newgame)
			# ISO boots rebuild argv from gamecube.cfg; Dolphin -- guest args
			# do not reach the DOL, so bake G94 into the disc override.
			if [[ "${DOLPHIN_G94:-0}" == "1" ]]; then
				BUILD_ARGS+=(--probe-newsaveload)
			fi
			if [[ "${DOLPHIN_FULLPHYSICS:-0}" == "1" ]]; then
				BUILD_ARGS+=(--probe-fullphysics)
				echo "==> Native full server/physics probe"
			fi
		fi
		if [[ "${DOLPHIN_MENU_NEWGAME:-0}" == "1" ]]; then
			BUILD_ARGS+=(--probe-menu-newgame)
			echo "==> Staging fallback-menu New Game override"
		fi
	elif [[ -n "$SMOKE_MAP" ]]; then
		BUILD_ARGS+=(--smoke-map "$SMOKE_MAP")
		if [[ "${DOLPHIN_G94:-0}" == "1" ]]; then
			BUILD_ARGS+=(--probe-newsaveload)
			echo "==> Staging G94 save/load override on smoke map"
		fi
		if (( DOLPHIN_NEWGAME )) && [[ "${DOLPHIN_FULLPHYSICS:-0}" == "1" ]]; then
			BUILD_ARGS+=(--probe-fullphysics)
			echo "==> Native full server/physics probe on smoke map"
		fi
		if (( DOLPHIN_WORLD_RENDER )); then
			BUILD_ARGS+=(--world-render)
			echo "==> World render probe mode (gcworldrender in gamecube.cfg)"
		fi
	fi
	if [[ -n "$GC_PHASE_TEST" ]]; then
		BUILD_ARGS+=(--probe-phasetest "$GC_PHASE_TEST")
		echo "==> G82 phase-fault probe (phasetest ${GC_PHASE_TEST})"
	fi
	if [[ -n "$DOLPHIN_CHANGELEVEL" ]]; then
		BUILD_ARGS+=(--probe-changelevel "$DOLPHIN_CHANGELEVEL")
		echo "==> G68 changelevel probe (to ${DOLPHIN_CHANGELEVEL})"
		if [[ -n "${DOLPHIN_LANDMARK:-}" ]]; then
			BUILD_ARGS+=(--probe-landmark "$DOLPHIN_LANDMARK")
			echo "==> G97–G100 landmark probe (${DOLPHIN_LANDMARK})"
		fi
		if [[ "${DOLPHIN_G101:-0}" == "1" ]]; then
			BUILD_ARGS+=(--probe-leanpvs)
			echo "==> G101 lean-N PVS probe (leanpvs in gamecube.cfg)"
		fi
	fi
	if ! python3 scripts/build-gamecube-disc.py "${BUILD_ARGS[@]}"; then
		echo "FAIL: Disc build failed."
		exit 1
	fi
fi

DOLPHIN_CMD=()
DOLPHIN_IS_FLATPAK=0
GUEST_ARGS=()
if (( GC_FATAL_TEST )); then
	GUEST_ARGS+=("-gc_fatal_test" "1")
fi
if [[ -n "$GC_PHASE_TEST" ]]; then
	GUEST_ARGS+=("-gc_phase_test" "$GC_PHASE_TEST")
	echo "==> Waiting for G82 intentional phase fault at ${GC_PHASE_TEST}"
fi
if [[ -n "$DOLPHIN_CHANGELEVEL" ]]; then
	GUEST_ARGS+=("-gcchangelevel" "$DOLPHIN_CHANGELEVEL")
	G68_DONE_MARKER="Xash3D GameCube: G68 changelevel ready from=${SMOKE_MAP} to=${DOLPHIN_CHANGELEVEL}"
	FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-8}"
	echo "==> Waiting for G68 changelevel ready ${SMOKE_MAP}→${DOLPHIN_CHANGELEVEL}"
	if [[ "${DOLPHIN_G95:-0}" == "1" ]]; then
		G95_DONE_MARKER="Xash3D GameCube: newgame low-res world present map=${DOLPHIN_CHANGELEVEL}"
		FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-12}"
		echo "==> Waiting for G95 world present on ${DOLPHIN_CHANGELEVEL}"
	fi
	if [[ "${DOLPHIN_G96:-0}" == "1" || "${DOLPHIN_G101:-0}" == "1" ]]; then
		G96_DONE_MARKER="Xash3D GameCube: Capture FatPVS lean map=${DOLPHIN_CHANGELEVEL}"
		# G101 forces lean-N; do not accept full multi-cluster as success.
		if [[ "${DOLPHIN_G101:-0}" == "1" ]]; then
			G96_ALT_MARKER=""
		else
			G96_ALT_MARKER="Xash3D GameCube: Capture FatPVS map=${DOLPHIN_CHANGELEVEL}"
		fi
		FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-12}"
		echo "==> Waiting for G96 lean/full FatPVS capture on ${DOLPHIN_CHANGELEVEL}"
	fi
	if [[ "${DOLPHIN_G101:-0}" == "1" ]]; then
		G101_DONE_MARKER="Xash3D GameCube: PVS lean follow ready"
		G101_ALT_MARKER=""
		FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-12}"
		echo "==> Waiting for G101 lean-N PVS follow on ${DOLPHIN_CHANGELEVEL}"
	fi
	if [[ "${DOLPHIN_G97:-0}" == "1" || "${DOLPHIN_G98:-0}" == "1" || "${DOLPHIN_G99:-0}" == "1" || "${DOLPHIN_G100:-0}" == "1" || "${DOLPHIN_G102:-0}" == "1" || "${DOLPHIN_G103:-0}" == "1" || "${DOLPHIN_G104:-0}" == "1" || "${DOLPHIN_G105:-0}" == "1" ]]; then
		G97_DONE_MARKER="Xash3D GameCube: G97 landmark restore health=77"
		G98_DONE_MARKER="Xash3D GameCube: G98 landmark restore health=77 armor=50 weapons=0x6"
		G99_DONE_MARKER="Xash3D GameCube: G99 landmark restore health=77 armor=50 weapons=0x6 ammo1=99 ammo2=88"
		G100_DONE_MARKER="Xash3D GameCube: G100 landmark weapons granted=2"
		G102_DONE_MARKER="Xash3D GameCube: G102 landmark weapons granted=2"
		G103_DONE_MARKER="Xash3D GameCube: G103 landmark weapons granted=2"
		G104_DONE_MARKER="Xash3D GameCube: G104 landmark weapons granted=2"
		G104_DEPLOY_MARKER="Xash3D GameCube: G104 deploy viewmodel="
		G105_DONE_MARKER="Xash3D GameCube: G105 viewmodel draw"
		FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-12}"
		if [[ "${DOLPHIN_G105:-0}" == "1" ]]; then
			G97_DONE_MARKER=""
			G98_DONE_MARKER=""
			G99_DONE_MARKER=""
			G100_DONE_MARKER=""
			G102_DONE_MARKER=""
			G103_DONE_MARKER=""
			G104_DONE_MARKER=""
			G104_DEPLOY_MARKER=""
			echo "==> Waiting for G105 landmark viewmodel draw"
		elif [[ "${DOLPHIN_G104:-0}" == "1" ]]; then
			G97_DONE_MARKER=""
			G98_DONE_MARKER=""
			G99_DONE_MARKER=""
			G100_DONE_MARKER=""
			G102_DONE_MARKER=""
			G103_DONE_MARKER=""
			G105_DONE_MARKER=""
			echo "==> Waiting for G104 landmark Deploy/viewmodel grant"
		elif [[ "${DOLPHIN_G103:-0}" == "1" ]]; then
			G97_DONE_MARKER=""
			G98_DONE_MARKER=""
			G99_DONE_MARKER=""
			G100_DONE_MARKER=""
			G102_DONE_MARKER=""
			G104_DONE_MARKER=""
			G104_DEPLOY_MARKER=""
			G105_DONE_MARKER=""
			echo "==> Waiting for G103 landmark inventory-attach grant"
		elif [[ "${DOLPHIN_G102:-0}" == "1" ]]; then
			G97_DONE_MARKER=""
			G98_DONE_MARKER=""
			G99_DONE_MARKER=""
			G100_DONE_MARKER=""
			G103_DONE_MARKER=""
			G104_DONE_MARKER=""
			G104_DEPLOY_MARKER=""
			G105_DONE_MARKER=""
			echo "==> Waiting for G102 landmark weapon Spawn/Touch grant"
		elif [[ "${DOLPHIN_G100:-0}" == "1" ]]; then
			G97_DONE_MARKER=""
			G98_DONE_MARKER=""
			G99_DONE_MARKER=""
			G102_DONE_MARKER=""
			G103_DONE_MARKER=""
			G104_DONE_MARKER=""
			G104_DEPLOY_MARKER=""
			G105_DONE_MARKER=""
			echo "==> Waiting for G100 landmark weapon grant"
		elif [[ "${DOLPHIN_G99:-0}" == "1" ]]; then
			G97_DONE_MARKER=""
			G98_DONE_MARKER=""
			G100_DONE_MARKER=""
			G102_DONE_MARKER=""
			G103_DONE_MARKER=""
			G104_DONE_MARKER=""
			G104_DEPLOY_MARKER=""
			G105_DONE_MARKER=""
			echo "==> Waiting for G99 landmark ammo restore"
		elif [[ "${DOLPHIN_G98:-0}" == "1" ]]; then
			G97_DONE_MARKER=""
			G99_DONE_MARKER=""
			G100_DONE_MARKER=""
			G102_DONE_MARKER=""
			G103_DONE_MARKER=""
			G104_DONE_MARKER=""
			G104_DEPLOY_MARKER=""
			G105_DONE_MARKER=""
			echo "==> Waiting for G98 landmark inventory restore"
		else
			G98_DONE_MARKER=""
			G99_DONE_MARKER=""
			G100_DONE_MARKER=""
			G102_DONE_MARKER=""
			G103_DONE_MARKER=""
			G104_DONE_MARKER=""
			G104_DEPLOY_MARKER=""
			G105_DONE_MARKER=""
			echo "==> Waiting for G97 landmark health restore"
		fi
	fi
fi
if (( DOLPHIN_NEWGAME )); then
	GUEST_ARGS+=("-gcnewgame")
	SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0}"
	MAP_MARKER="Xash3D GameCube: map loaded ${SMOKE_MAP}"
	# Prefer G281 ride restream (map reading along tram).
	G278_DONE_MARKER="G281 cl="
	echo "==> New Game probe mode (expect map ${SMOKE_MAP})"
	echo "==> Waiting for G281 Flipper map restream (GPU faces follow tram)"
	if [[ -n "$DOLPHIN_CHANGELEVEL" ]]; then
		G68_DONE_MARKER="Xash3D GameCube: G68 changelevel ready from=${SMOKE_MAP} to=${DOLPHIN_CHANGELEVEL}"
		# G161 soft DumpFrames gun, then G159 sustained Flipper after reconnect.
		G158_DONE_MARKER="Xash3D GameCube: G158 live GX present reconnect"
		G159_DONE_MARKER="Xash3D GameCube: G159 live GX present ca_active"
		G161_DONE_MARKER="Xash3D GameCube: G161 soft dump viewmodel ready"
		G162_DONE_MARKER="Xash3D GameCube: G162 soft dump viewmodel framed"
		G163_DONE_MARKER="Xash3D GameCube: G163 refreshed draw faces="
		G164_DONE_MARKER="Xash3D GameCube: G164 studio gouraud shades="
		G165_DONE_MARKER="Xash3D GameCube: G165 restore refresh cluster="
		G166_DONE_MARKER="Xash3D GameCube: G166 soft studio rgb shades="
		G167_DONE_MARKER="Xash3D GameCube: G167 viewmodel depth range"
		G168_DONE_MARKER="Xash3D GameCube: G168 studio chrome uv samples="
		G169_DONE_MARKER="Xash3D GameCube: G169 soft studio scalar light"
		G170_DONE_MARKER="Xash3D GameCube: G170 soft studio chroma tint="
		G171_DONE_MARKER="Xash3D GameCube: G171 outdoor refresh"
		G172_DONE_MARKER="Xash3D GameCube: G172 HUD sheets loaded"
		G173_DONE_MARKER="Xash3D GameCube: G173 HUD hud1 lean"
		G174_DONE_MARKER="Xash3D GameCube: G174 HUD crosshairs lean"
		G175_DONE_MARKER="Xash3D GameCube: G175 outdoor refresh"
		G176_DONE_MARKER="Xash3D GameCube: G176 raised face cap"
		G177_DONE_MARKER="Xash3D GameCube: G177 soft dump HUD composite"
		G178_DONE_MARKER="Xash3D GameCube: G178 GX world state cache"
		G179_DONE_MARKER="Xash3D GameCube: G179 GX world sync lean"
		G180_DONE_MARKER="Xash3D GameCube: G180 GX lightmap atlas"
		G181_DONE_MARKER="Xash3D GameCube: G181 GX tex band order"
		G182_DONE_MARKER="Xash3D GameCube: G182 GX HUD stretch"
		G183_DONE_MARKER="Xash3D GameCube: G183 GX HUD rich"
		G184_DONE_MARKER="Xash3D GameCube: G184 GX HUD alpha holes"
		G185_DONE_MARKER="Xash3D GameCube: G185 GX HUD fill lean"
		G186_DONE_MARKER="Xash3D GameCube: G186 GX Flipper face cull"
		G187_DONE_MARKER="Xash3D GameCube: G187 GX HUD nearblack holes"
		G188_DONE_MARKER="Xash3D GameCube: G188 landmark Flipper continuity"
		G189_DONE_MARKER="Xash3D GameCube: G189 outdoor"
		G190_DONE_MARKER="Xash3D GameCube: G190"
		G191_DONE_MARKER="Xash3D GameCube: G191 soft dump EFB"
		G192_DONE_MARKER="Xash3D GameCube: G192 DumpFrames re-arm ready"
		G193_DONE_MARKER="Xash3D GameCube: G193 dual-XFB soft latch ready"
		G194_DONE_MARKER="Xash3D GameCube: G194 soft DumpFrames stamp ready"
		G195_DONE_MARKER="Xash3D GameCube: G195 Flipper resume after soft DumpFrames"
		G196_DONE_MARKER="Xash3D GameCube: G196 Flipper dump wall-aim ready"
		# G194–G196: soft latch, Flipper resume, wall-aim DumpFrames.
		FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-45}"
		echo "==> Waiting for G196 Flipper wall-aim + G195/G194/G193/G192/G191/G190/G189/G188/G187/G186/G185/G184/G183/G182/G181/G180/G179/G178/G177/G176/G175/G174/G173/G172/G171/G170/G169/G168/G167/G166/G165/G164/G163/G162/G161/G159 markers"
	fi
	if [[ "${DOLPHIN_G94:-0}" == "1" ]]; then
		GUEST_ARGS+=("-gcnewsaveload")
		echo "==> G94 save/load probe (-gcnewsaveload, RAM bank if no SD)"
		# Keep sampling until post-load world present (not just G36 arming).
		G94_DONE_MARKER="Xash3D GameCube: G94 round trip present"
		FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-30}"
		echo "==> Waiting for G94 round trip present before sampling exit"
	fi
fi
append_guest_args() {
	local -n _cmd="$1"
	if ((${#GUEST_ARGS[@]})); then
		_cmd+=(-- "${GUEST_ARGS[@]}")
	fi
}

DOLPHIN_VIDEO_BACKEND="${DOLPHIN_VIDEO:-Null}"
DOLPHIN_BATCH_MODE=1
if [[ "$DOLPHIN_RETAIL" == "1" ]]; then
	DOLPHIN_VIDEO_BACKEND="${DOLPHIN_VIDEO:-OpenGL}"
	if [[ "${DOLPHIN_CAPTURE_MENU:-0}" == "1" ]]; then
		DOLPHIN_BATCH_MODE=0
		TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-120}"
	fi
fi
DOLPHIN_MODE_ARGS=()
if (( DOLPHIN_BATCH_MODE )); then
	DOLPHIN_MODE_ARGS=(-l -b)
else
	DOLPHIN_MODE_ARGS=(-l)
fi

if [[ "${DOLPHIN_EXECUTABLE:-}" == flatpak:* ]]; then
	DOLPHIN_FLATPAK_ID="${DOLPHIN_EXECUTABLE#flatpak:}"
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "$DOLPHIN_FLATPAK_ID"
		-u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
	DOLPHIN_IS_FLATPAK=1
elif [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
	DOLPHIN_CMD=("$DOLPHIN_EXECUTABLE" -u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
elif [[ -x "$ROOT/3rdparty/dolphin/build/Binaries/dolphin-emu" ]]; then
	# G192: local tree build includes XFB RAM re-decode when DisableCopyToVRAM
	# is set; Flatpak/stock Dolphin DumpFrames stay on a stale sky XFB.
	DOLPHIN_CMD=("$ROOT/3rdparty/dolphin/build/Binaries/dolphin-emu" -u "$USER_DIR"
		"${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
	echo "==> Using local Dolphin: ${DOLPHIN_CMD[0]}"
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin-emu -u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
elif command -v dolphin >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin -u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
elif command -v flatpak >/dev/null 2>&1 && \
	flatpak info "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1; then
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}"
		-u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
	DOLPHIN_IS_FLATPAK=1
else
	echo "HOST_FAILURE: Dolphin executable or Flatpak was not found."
	exit 2
fi

cleanup_flatpak_dolphin() {
	if (( DOLPHIN_IS_FLATPAK )); then
		flatpak kill "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1 || true
		pkill -TERM -f "dolphin.*${USER_DIR}" >/dev/null 2>&1 || true
		sleep 1
		pkill -KILL -f "dolphin.*${USER_DIR}" >/dev/null 2>&1 || true
	fi
}

echo "==> Launching bounded Dolphin boot probe (${TIMEOUT_SEC}s, MMU=${DOLPHIN_MMU})..."
set +e
if (( DOLPHIN_IS_FLATPAK )); then
	probe_wait_flatpak
else
	probe_wait_native
fi
set -e

sleep 2
if (( DOLPHIN_IS_FLATPAK )); then
	cleanup_flatpak_dolphin
	trap - EXIT
	wait "$DOLPHIN_WRAPPER_PID" >/dev/null 2>&1 || true
fi

echo "==> Analyzing probe results..."
LOG_FILES=("$LOG_DIR/stdout.log" "$LOG_DIR/stderr.log")
for guest_log in "$LOG_DIR"/dolphin-user/Logs/*.log; do
	[[ -f "$guest_log" ]] && LOG_FILES+=("$guest_log")
done
GUEST_FOUND=0 READY_FOUND=0 MAP_FOUND=0 INPUT_FOUND=0
PLAY_READY_FOUND=0 FRAME_ARMED_FOUND=0 NEWGAME_PROGRESS_FOUND=0
grep -aqsF "$GUEST_MARKER" "${LOG_FILES[@]}" && GUEST_FOUND=1
grep -aqsF "$READY_MARKER" "${LOG_FILES[@]}" && READY_FOUND=1
grep -aqsF "$INPUT_MARKER" "${LOG_FILES[@]}" && INPUT_FOUND=1
grep -aqsF "$PLAY_READY_MARKER" "${LOG_FILES[@]}" && PLAY_READY_FOUND=1
grep -aqsF "$FRAME_ARMED_MARKER" "${LOG_FILES[@]}" && FRAME_ARMED_FOUND=1
if probe_newgame_progress_ready; then
	NEWGAME_PROGRESS_FOUND=1
fi
if [[ -n "$SMOKE_MAP" ]]; then
	grep -aqsF "$MAP_MARKER" "${LOG_FILES[@]}" && MAP_FOUND=1
fi

if (( GC_FATAL_TEST )) && probe_log_has "$G37_FATAL_MARKER" && probe_log_has "$GUEST_MARKER"; then
	echo "G37_VERIFIED: Intentional fatal error triggered and breadcrumb reported."
	echo "Logs: $LOG_DIR"
	finalize_probe g37_verified 0
fi

if [[ -n "$GC_PHASE_TEST" ]] && [[ -n "$G82_FAULT_MARKER" ]] \
	&& probe_log_has "$G82_FAULT_MARKER" \
	&& probe_log_has "boot phase=${GC_PHASE_TEST}" \
	&& grep -aqsE "boot=${GC_PHASE_TEST}([[:space:]]|$)" "${LOG_FILES[@]}"; then
	echo "G82_VERIFIED: last_successful_phase=${GC_PHASE_TEST} fault_at=${GC_PHASE_TEST}"
	echo "Logs: $LOG_DIR"
	finalize_probe g82_verified 0
fi

if [[ -n "$GC_PHASE_TEST" ]]; then
	echo "G82_FAIL: expected intentional phase fault at ${GC_PHASE_TEST} with boot breadcrumb."
	echo "Logs: $LOG_DIR"
	finalize_probe g82_fail 3
fi

RETAIL_MENU_SEEN=0
RETAIL_MENU_READY=0
if probe_retail_menu_seen; then
	RETAIL_MENU_SEEN=1
fi
if probe_retail_menu_ready; then
	RETAIL_MENU_READY=1
fi

if (( RETAIL_MENU_READY )) && [[ "$DOLPHIN_RETAIL" == "1" ]] && (( ! DOLPHIN_NEWGAME )); then
		probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Retail boot reached menu, followed by a guest error."
		if probe_log_has "$INTRO_MARKER"; then
			echo "RETAIL_READY: Half-Life retail boot played intro AVI and reached the interactive menu on GameCube."
	else
		echo "RETAIL_READY: Half-Life retail boot reached the interactive menu on GameCube (intro AVI marker not seen)."
	fi
		probe_report_g45
		echo "Logs: $LOG_DIR"
		finalize_probe retail_ready 0
fi

if [[ -n "$DOLPHIN_CHANGELEVEL" ]] \
	&& probe_log_has "$G68_DONE_MARKER" \
	&& probe_log_has "Xash3D GameCube: MAP_READY ${DOLPHIN_CHANGELEVEL}" \
	&& probe_log_has "Xash3D GameCube: G100 landmark restore"; then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Changelevel reached its destination, followed by a guest error."
	if [[ -n "${G94_DONE_MARKER:-}" ]] && ! probe_log_has "$G94_DONE_MARKER"; then
		echo "CHANGELEVEL_PARTIAL_READY: Destination and landmark restore markers were present, but G94 did not complete."
		echo "Logs: $LOG_DIR"
		finalize_probe changelevel_partial_ready 4
	fi
	echo "CHANGELEVEL_READY: Destination map, landmark state, and required runtime continuity markers passed."
	probe_report_g45
	echo "Logs: $LOG_DIR"
	finalize_probe changelevel_ready 0
fi

if (( MAP_FOUND )) && (( INPUT_FOUND )) && (( !DOLPHIN_NEWGAME || ( PLAY_READY_FOUND && FRAME_ARMED_FOUND ) || NEWGAME_PROGRESS_FOUND )); then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Map load was observed, followed by a guest error."
	if (( NEWGAME_PROGRESS_FOUND )) && (( !PLAY_READY_FOUND || !FRAME_ARMED_FOUND )); then
		echo "NEWGAME_READY: Sustained world, gameplay input, and attack/jump/use actions were observed on ${SMOKE_MAP}."
	else
		echo "MAP_READY: Xash3D loaded ${SMOKE_MAP} on GameCube with interactive input."
	fi
	probe_report_g45
	echo "Logs: $LOG_DIR"
	if (( NEWGAME_PROGRESS_FOUND )); then
		finalize_probe newgame_ready 0
	else
		finalize_probe map_ready 0
	fi
fi

if (( DOLPHIN_NEWGAME )) && (( MAP_FOUND )) && (( INPUT_FOUND )) && (( PLAY_READY_FOUND )) && (( !FRAME_ARMED_FOUND )); then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: New Game reached play-start, followed by a guest error before frame-budget arming."
	echo "NEWGAME_PARTIAL_READY: Map ${SMOKE_MAP} loaded and play-start completed, but post-map frame-budget arming was not observed."
	echo "Logs: $LOG_DIR"
	finalize_probe newgame_partial_ready 4
fi

if (( DOLPHIN_NEWGAME )) && (( MAP_FOUND )) && (( INPUT_FOUND )) \
	&& (( !PLAY_READY_FOUND || !FRAME_ARMED_FOUND )); then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Map and input were ready, but New Game did not reach play-start/frame-budget arming."
	echo "NEWGAME_PARTIAL_READY: Map ${SMOKE_MAP} loaded and input became active, but post-map play-start/frame-budget markers were incomplete."
	echo "Logs: $LOG_DIR"
	finalize_probe newgame_partial_ready 4
fi

if (( MAP_FOUND )) && ! (( INPUT_FOUND )); then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Map load was observed, followed by a guest error."
	echo "MAP_LOADED_NO_INPUT: Map ${SMOKE_MAP} loaded but input polling marker was not found."
	echo "Logs: $LOG_DIR"
	finalize_probe map_loaded_no_input 0
fi

if (( READY_FOUND )) && [[ -z "$SMOKE_MAP" ]] && [[ "$DOLPHIN_RETAIL" != "1" ]]; then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Engine readiness was observed, followed by a guest error."
	echo "ENGINE_READY: Xash3D initialized its GameCube subsystems."
	echo "Logs: $LOG_DIR"
	finalize_probe engine_ready 0
fi

if (( READY_FOUND )) && (( GUEST_FOUND )) && (( DOLPHIN_NEWGAME )) && ! (( MAP_FOUND )); then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: New Game bootstrap reached engine readiness, followed by a guest error before map load."
	echo "NEWGAME_EARLY_EXIT: Engine readiness was observed, but New Game exited before ${SMOKE_MAP:-the map} loaded."
	grep -ahF 'OSREPORT' "${LOG_FILES[@]}" | tail -1 | sed 's/^/Last guest log: /'
	echo "Logs: $LOG_DIR"
	finalize_probe newgame_early_exit 4
fi

if (( RETAIL_MENU_SEEN )) && [[ "${DOLPHIN_REQUIRE_MENU_ACTIONS:-0}" == "1" ]] && ! (( RETAIL_MENU_READY )); then
	echo "RETAIL_MENU_WAIT: retail menu reached readiness markers, but synthetic menu actions did not complete."
	echo "Logs: $LOG_DIR"
	finalize_probe retail_menu_wait 4
fi

if (( GUEST_FOUND )) && probe_guest_error && (( ! GC_FATAL_TEST )) && [[ -z "$GC_PHASE_TEST" ]]; then
	probe_fail_guest guest_failure "GUEST_FAILURE: Bootstrap was followed by a guest-engine error."
fi

if grep -aEiq 'Unknown instruction|Invalid read from|IntCPU:|apploader.*(fail|error)' "${LOG_FILES[@]}"; then
	probe_fail_guest boot_failure "BOOT_FAILURE: Dolphin reached the disc but the guest image failed before bootstrap."
fi

if (( DOLPHIN_EXIT == 124 || DOLPHIN_EXIT == 137 )); then
	if [[ -n "$SMOKE_MAP" ]] && (( READY_FOUND )); then
		echo "MAP_TIMEOUT: Engine readiness was observed, but ${SMOKE_MAP} did not load within ${TIMEOUT_SEC}s."
	elif (( GUEST_FOUND )); then
		echo "GUEST_TIMEOUT: Bootstrap was observed, but engine readiness was not reached within ${TIMEOUT_SEC}s."
	else
		echo "INCONCLUSIVE_TIMEOUT: No guest bootstrap within ${TIMEOUT_SEC}s."
	fi
	grep -ahF 'OSREPORT' "${LOG_FILES[@]}" | tail -1 | sed 's/^/Last guest log: /'
	echo "Logs: $LOG_DIR"
	finalize_probe map_timeout 4
fi

if (( DOLPHIN_EXIT != 0 )); then
	if (( GUEST_FOUND )) && (( ! GC_FATAL_TEST )); then
		probe_fail_guest guest_failure "GUEST_FAILURE: Dolphin exited $DOLPHIN_EXIT after guest bootstrap."
	fi
	if (( ! GUEST_FOUND )); then
		probe_fail_guest host_failure "HOST_FAILURE: Dolphin exited $DOLPHIN_EXIT before guest bootstrap."
	fi
fi

# Landmark G16x New Game often skips play-start / frame-armed; MAP+INPUT is enough
# once the wait loop has already observed Flipper/soft-dump done markers.
if (( MAP_FOUND )) && (( INPUT_FOUND )) && (( DOLPHIN_NEWGAME )); then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Map load was observed, followed by a guest error."
	echo "MAP_READY: Xash3D loaded ${SMOKE_MAP} on GameCube with interactive input."
	probe_report_g45
	echo "Logs: $LOG_DIR"
	finalize_probe map_ready 0
fi

if (( ! MAP_FOUND )) && (( ! READY_FOUND )) && (( ! GUEST_FOUND )); then
	echo "INCONCLUSIVE_EXIT: Dolphin exited $DOLPHIN_EXIT without reaching engine readiness."
	(( GUEST_FOUND )) && grep -ahF 'OSREPORT' "${LOG_FILES[@]}" | tail -1 | sed 's/^/Last guest log: /'
	echo "Logs: $LOG_DIR"
	finalize_probe inconclusive_exit 4
fi

echo "INCONCLUSIVE_EXIT: Dolphin exited $DOLPHIN_EXIT without a classified probe status."
echo "Logs: $LOG_DIR"
finalize_probe inconclusive_exit 4
