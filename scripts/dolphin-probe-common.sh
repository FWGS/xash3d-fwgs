# Shared helpers for dolphin-boot-probe.sh
# shellcheck shell=bash

probe_log_has() {
	local needle="$1"
	grep -aqsF "$needle" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" \
		"$LOG_DIR"/dolphin-user/Logs/*.log 2>/dev/null
}

probe_guest_error() {
	grep -aEiq 'Host_Error|Sys_Error|Xash Error|_Mem_Alloc: out of memory|fatal error|guest.*(crash|abort)|Invalid read from|MMU fault|Program attempting to read|trashed (small )?header sentinel' \
		"$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" \
		"$LOG_DIR"/dolphin-user/Logs/*.log 2>/dev/null
}

probe_retail_menu_seen() {
	[[ "${DOLPHIN_RETAIL:-0}" == "1" ]] && (( ! DOLPHIN_NEWGAME )) && \
		( probe_log_has "$RETAIL_MENU_INTERACTIVE_MARKER" || probe_log_has "$RETAIL_MENU_MARKER" || \
		probe_log_has "$RETAIL_MENU_BG_FALLBACK_MARKER" || probe_log_has "$RETAIL_MENU_READY_FALLBACK_MARKER" )
}

probe_retail_menu_ready() {
	if ! probe_retail_menu_seen; then
		return 1
	fi

	if [[ "${DOLPHIN_REQUIRE_MENU_ACTIONS:-0}" == "1" ]]; then
		probe_log_has "$MENU_ACTION_READY_MARKER"
		return
	fi

	return 0
}

finalize_probe() {
	local status="$1"
	local exit_code="$2"
	timeout --signal=TERM --kill-after=5 60 python3 scripts/dolphin-probe-analyze.py \
		--repo "$ROOT" \
		--log-dir "$LOG_DIR" \
		--smoke-map "$SMOKE_MAP" \
		--probe-status "$status" \
		--update-state || echo "WARNING: dolphin-probe-analyze timed out or failed"
	exit "$exit_code"
}

probe_report_g45() {
	if (( INPUT_FOUND )); then
		echo "G45_STATUS: PASS"
	elif probe_log_has "$G45_READY_MARKER"; then
		grep -ahF "$G45_READY_MARKER" "${LOG_FILES[@]}" | tail -1
		echo "G45_STATUS: PASS"
	elif probe_log_has "$G45_WAIT_MARKER"; then
		echo "G45_STATUS: WAIT"
	else
		echo "G45_STATUS: WEAK"
	fi
}

probe_fail_guest() {
	local status="$1"
	local message="$2"
	echo "$message"
	echo "Logs: $LOG_DIR"
	finalize_probe "$status" 3
}

# -gcnewgame can take the direct client path and therefore does not emit the
# legacy server-side play-start/frame-arm pair.  Require independent runtime
# evidence instead: a sustained presented world, gameplay input, and all three
# scripted gameplay actions. This is intentionally stricter than map-loaded.
probe_newgame_progress_ready() {
	(( DOLPHIN_NEWGAME )) || return 1
	# Specialized probes must reach their own terminal marker before the
	# generic gameplay shortcut can stop Dolphin (notably G94 save/load).
	if [[ -n "${G94_DONE_MARKER:-}" ]] && ! probe_log_has "$G94_DONE_MARKER"; then
		return 1
	fi
	if [[ -n "${G508_DONE_MARKER:-}" ]] && ! probe_log_has "$G508_DONE_MARKER"; then
		return 1
	fi
	if [[ -n "${TAS_DONE_MARKER:-}" ]] && ! probe_log_has "$TAS_DONE_MARKER"; then
		return 1
	fi
	# Prefer frames=32 (tram). Reactor maps tip-safe-stop around SCR 16 after
	# post-G36 sustain (c3a2 20260809-012506) — accept 16 when actions landed.
	probe_log_has "Xash3D GameCube: post-G36 sustained world present" \
		&& { probe_log_has "Xash3D GameCube: newgame sustained frames=32" \
			|| probe_log_has "Xash3D GameCube: newgame sustained frames=16"; } \
		&& probe_log_has "Xash3D GameCube: probe gameplay input ready" \
		&& probe_log_has "Xash3D GameCube: probe gameplay action attack" \
		&& probe_log_has "Xash3D GameCube: probe gameplay action jump" \
		&& probe_log_has "Xash3D GameCube: probe gameplay action use"
}

probe_wait_flatpak() {
	flatpak kill "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1 || true
	trap cleanup_flatpak_dolphin EXIT
	"${DOLPHIN_CMD[@]}" >"$LOG_DIR/stdout.log" 2>"$LOG_DIR/stderr.log" &
	DOLPHIN_WRAPPER_PID=$!
	DOLPHIN_EXIT=124
	local deadline=$(( $(date +%s) + TIMEOUT_SEC ))
	local map_ready_at=0 retail_ready_at=0 g94_sample_armed=0 g278_sample_armed=0
	while (( $(date +%s) < deadline )); do
		if [[ -n "${G82_FAULT_MARKER:-}" ]] && probe_log_has "$G82_FAULT_MARKER"; then
			DOLPHIN_EXIT=0; break
		fi
		if probe_log_has "$MAP_MARKER" && probe_log_has "$INPUT_MARKER"; then
			if probe_newgame_progress_ready; then
				if probe_guest_error; then DOLPHIN_EXIT=3; break; fi
				DOLPHIN_EXIT=0; break
			fi
			if (( DOLPHIN_NEWGAME )); then
				# Landmark Flipper/G16x markers prove play progressed past
				# "play start ready" / frame-budget arm (often omitted on -gcnewgame).
				if [[ -z "${G159_DONE_MARKER:-}" && -z "${G161_DONE_MARKER:-}" && -z "${G162_DONE_MARKER:-}" && -z "${G163_DONE_MARKER:-}" && -z "${G164_DONE_MARKER:-}" && -z "${G165_DONE_MARKER:-}" && -z "${G166_DONE_MARKER:-}" && -z "${G167_DONE_MARKER:-}" && -z "${G168_DONE_MARKER:-}" && -z "${G169_DONE_MARKER:-}" && -z "${G170_DONE_MARKER:-}" && -z "${G171_DONE_MARKER:-}" && -z "${G172_DONE_MARKER:-}" && -z "${G173_DONE_MARKER:-}" && -z "${G174_DONE_MARKER:-}" && -z "${G175_DONE_MARKER:-}" && -z "${G176_DONE_MARKER:-}" && -z "${G177_DONE_MARKER:-}" && -z "${G178_DONE_MARKER:-}" && -z "${G179_DONE_MARKER:-}" && -z "${G180_DONE_MARKER:-}" && -z "${G181_DONE_MARKER:-}" && -z "${G182_DONE_MARKER:-}" && -z "${G183_DONE_MARKER:-}" && -z "${G184_DONE_MARKER:-}" && -z "${G185_DONE_MARKER:-}" && -z "${G186_DONE_MARKER:-}" && -z "${G187_DONE_MARKER:-}" && -z "${G188_DONE_MARKER:-}" && -z "${G189_DONE_MARKER:-}" && -z "${G190_DONE_MARKER:-}" && -z "${G191_DONE_MARKER:-}" && -z "${G192_DONE_MARKER:-}" ]]; then
					if ! probe_log_has "${PLAY_READY_MARKER:-Xash3D GameCube: play start ready}"; then
						sleep 2
						continue
					fi
					if [[ -n "${FRAME_ARMED_MARKER:-}" ]] && ! probe_log_has "$FRAME_ARMED_MARKER"; then
						sleep 2
						continue
					fi
				fi
			fi
			(( map_ready_at == 0 )) && map_ready_at=$(date +%s)
			if probe_guest_error; then DOLPHIN_EXIT=3; break; fi
			# G94: do not stop until post-load world present is observed.
			if [[ -n "${G94_DONE_MARKER:-}" ]] && ! probe_log_has "$G94_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G508: wait for config write/read markers on the designated route.
			if [[ -n "${G508_DONE_MARKER:-}" ]] && ! probe_log_has "$G508_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${TAS_DONE_MARKER:-}" ]] && ! probe_log_has "$TAS_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G68: do not stop until changelevel destination is ready.
			if [[ -n "${G68_DONE_MARKER:-}" ]] && ! probe_log_has "$G68_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G95: after changelevel, wait for destination world present.
			if [[ -n "${G95_DONE_MARKER:-}" ]] && ! probe_log_has "$G95_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G96: lean or full FatPVS capture on destination map.
			if [[ -n "${G96_DONE_MARKER:-}" ]]; then
				if ! probe_log_has "$G96_DONE_MARKER" \
					&& { [[ -z "${G96_ALT_MARKER:-}" ]] || ! probe_log_has "$G96_ALT_MARKER"; }; then
					sleep 2
					continue
				fi
			fi
			# G97: landmark health continuity across changelevel.
			if [[ -n "${G97_DONE_MARKER:-}" ]] && ! probe_log_has "$G97_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G98: landmark weapons/armor continuity across changelevel.
			if [[ -n "${G98_DONE_MARKER:-}" ]] && ! probe_log_has "$G98_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G99: landmark ammo private-data continuity across changelevel.
			if [[ -n "${G99_DONE_MARKER:-}" ]] && ! probe_log_has "$G99_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G100: landmark weapon-entity re-grant across changelevel.
			if [[ -n "${G100_DONE_MARKER:-}" ]] && ! probe_log_has "$G100_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G102: landmark weapon Spawn/Touch grant across changelevel.
			if [[ -n "${G102_DONE_MARKER:-}" ]] && ! probe_log_has "$G102_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G103: landmark inventory-chain weapon attach across changelevel.
			if [[ -n "${G103_DONE_MARKER:-}" ]] && ! probe_log_has "$G103_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G104: landmark lean Deploy/viewmodel after inventory attach.
			if [[ -n "${G104_DONE_MARKER:-}" ]] && ! probe_log_has "$G104_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G104_DEPLOY_MARKER:-}" ]] && ! probe_log_has "$G104_DEPLOY_MARKER"; then
				sleep 2
				continue
			fi
			# G105: landmark first-person viewmodel draw after Deploy.
			if [[ -n "${G105_DONE_MARKER:-}" ]] && ! probe_log_has "$G105_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G158: live Flipper presents after loopback reconnect.
			if [[ -n "${G158_DONE_MARKER:-}" ]] && ! probe_log_has "$G158_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G159: sustained Flipper presents after post-reconnect ca_active.
			if [[ -n "${G159_DONE_MARKER:-}" ]] && ! probe_log_has "$G159_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G161: soft DumpFrames viewmodel composite while Flipper is live.
			if [[ -n "${G161_DONE_MARKER:-}" ]] && ! probe_log_has "$G161_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G162: soft VM framed (offset + top VIEWMODEL panel).
			if [[ -n "${G162_DONE_MARKER:-}" ]] && ! probe_log_has "$G162_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G163: live cluster face refresh without LM rebake.
			if [[ -n "${G163_DONE_MARKER:-}" ]] && ! probe_log_has "$G163_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G164: GX studio Gouraud shading (per-vertex light).
			if [[ -n "${G164_DONE_MARKER:-}" ]] && ! probe_log_has "$G164_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G165: restore-cluster face refresh (player-eye cands).
			if [[ -n "${G165_DONE_MARKER:-}" ]] && ! probe_log_has "$G165_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G166: soft DumpFrames studio RGB lighting (not grey-ramp).
			if [[ -n "${G166_DONE_MARKER:-}" ]] && ! probe_log_has "$G166_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G167: GX viewmodel compressed depth range (not Z-always).
			if [[ -n "${G167_DONE_MARKER:-}" ]] && ! probe_log_has "$G167_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G168: Flipper studio chrome sphere UVs.
			if [[ -n "${G168_DONE_MARKER:-}" ]] && ! probe_log_has "$G168_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G169: soft studio scalar light + constant tint (no span noise).
			if [[ -n "${G169_DONE_MARKER:-}" ]] && ! probe_log_has "$G169_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G170: soft studio chroma tint proof (non-white DumpFrames light).
			if [[ -n "${G170_DONE_MARKER:-}" ]] && ! probe_log_has "$G170_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G171: outdoor Flipper refresh via slots↔cands trade.
			if [[ -n "${G171_DONE_MARKER:-}" ]] && ! probe_log_has "$G171_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G172: HUD sheets loaded real (not soft-fail stubs).
			if [[ -n "${G172_DONE_MARKER:-}" ]] && ! probe_log_has "$G172_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G173: lean 320hud1 sheet real (not soft-fail stub).
			if [[ -n "${G173_DONE_MARKER:-}" ]] && ! probe_log_has "$G173_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G174: lean crosshairs sheet real (not soft-fail stub).
			if [[ -n "${G174_DONE_MARKER:-}" ]] && ! probe_log_has "$G174_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G175: outdoor Flipper refresh via 4x64 slots/cands trade.
			if [[ -n "${G175_DONE_MARKER:-}" ]] && ! probe_log_has "$G175_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G176: raised face cap via LM 8→4 trade.
			if [[ -n "${G176_DONE_MARKER:-}" ]] && ! probe_log_has "$G176_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G177: soft DumpFrames HUD composite (lean sheets).
			if [[ -n "${G177_DONE_MARKER:-}" ]] && ! probe_log_has "$G177_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G178: cached GX world TEV/vtx state.
			if [[ -n "${G178_DONE_MARKER:-}" ]] && ! probe_log_has "$G178_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G179: lean GX world sync (Flush + LM texobj cache).
			if [[ -n "${G179_DONE_MARKER:-}" ]] && ! probe_log_has "$G179_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G180_DONE_MARKER:-}" ]] && ! probe_log_has "$G180_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G181_DONE_MARKER:-}" ]] && ! probe_log_has "$G181_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G182_DONE_MARKER:-}" ]] && ! probe_log_has "$G182_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G183_DONE_MARKER:-}" ]] && ! probe_log_has "$G183_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G184_DONE_MARKER:-}" ]] && ! probe_log_has "$G184_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G185_DONE_MARKER:-}" ]] && ! probe_log_has "$G185_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G186_DONE_MARKER:-}" ]] && ! probe_log_has "$G186_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G187_DONE_MARKER:-}" ]] && ! probe_log_has "$G187_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G188_DONE_MARKER:-}" ]] && ! probe_log_has "$G188_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G189_DONE_MARKER:-}" ]] && ! probe_log_has "$G189_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G190_DONE_MARKER:-}" ]] && ! probe_log_has "$G190_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G191_DONE_MARKER:-}" ]] && ! probe_log_has "$G191_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G192_DONE_MARKER:-}" ]] && ! probe_log_has "$G192_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G193_DONE_MARKER:-}" ]] && ! probe_log_has "$G193_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G194_DONE_MARKER:-}" ]] && ! probe_log_has "$G194_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G195_DONE_MARKER:-}" ]] && ! probe_log_has "$G195_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G196_DONE_MARKER:-}" ]] && ! probe_log_has "$G196_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G278/G280: New Game Flipper present / intro markers.
			if [[ -n "${G278_DONE_MARKER:-}" ]] && ! probe_log_has "$G278_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# Always restart the sample clock on first G280/G278 sight — do not
			# share g94_sample_armed (may already be set from map-ready path).
			if [[ -n "${G278_DONE_MARKER:-}" ]] && probe_log_has "$G278_DONE_MARKER" && (( g278_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g278_sample_armed=1
				g94_sample_armed=1
			fi
			# G101: lean-N multi-cluster PVS follow after changelevel.
			if [[ -n "${G101_DONE_MARKER:-}" ]]; then
				if ! probe_log_has "$G101_DONE_MARKER" \
					&& { [[ -z "${G101_ALT_MARKER:-}" ]] || ! probe_log_has "$G101_ALT_MARKER"; }; then
					sleep 2
					continue
				fi
			fi
			# Once G94 restore present is seen, restart the sample window.
			if [[ -n "${G94_DONE_MARKER:-}" ]] && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			fi
			if [[ -n "${G159_DONE_MARKER:-}" ]] && probe_log_has "$G159_DONE_MARKER" && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			elif [[ -n "${G158_DONE_MARKER:-}" ]] && probe_log_has "$G158_DONE_MARKER" && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			fi
			if [[ -n "${G68_DONE_MARKER:-}" || -n "${G95_DONE_MARKER:-}" || -n "${G96_DONE_MARKER:-}" || -n "${G97_DONE_MARKER:-}" || -n "${G98_DONE_MARKER:-}" || -n "${G99_DONE_MARKER:-}" || -n "${G100_DONE_MARKER:-}" || -n "${G101_DONE_MARKER:-}" || -n "${G102_DONE_MARKER:-}" || -n "${G103_DONE_MARKER:-}" || -n "${G104_DONE_MARKER:-}" || -n "${G105_DONE_MARKER:-}" || -n "${G158_DONE_MARKER:-}" || -n "${G159_DONE_MARKER:-}" || -n "${G161_DONE_MARKER:-}" || -n "${G162_DONE_MARKER:-}" || -n "${G163_DONE_MARKER:-}" || -n "${G164_DONE_MARKER:-}" || -n "${G165_DONE_MARKER:-}" || -n "${G166_DONE_MARKER:-}" || -n "${G167_DONE_MARKER:-}" || -n "${G168_DONE_MARKER:-}" || -n "${G169_DONE_MARKER:-}" || -n "${G170_DONE_MARKER:-}" || -n "${G171_DONE_MARKER:-}" || -n "${G172_DONE_MARKER:-}" || -n "${G173_DONE_MARKER:-}" || -n "${G174_DONE_MARKER:-}" || -n "${G175_DONE_MARKER:-}" || -n "${G176_DONE_MARKER:-}" || -n "${G177_DONE_MARKER:-}" || -n "${G178_DONE_MARKER:-}" || -n "${G179_DONE_MARKER:-}" || -n "${G180_DONE_MARKER:-}" || -n "${G181_DONE_MARKER:-}" || -n "${G182_DONE_MARKER:-}" || -n "${G183_DONE_MARKER:-}" || -n "${G184_DONE_MARKER:-}" || -n "${G185_DONE_MARKER:-}" || -n "${G186_DONE_MARKER:-}" || -n "${G187_DONE_MARKER:-}" || -n "${G188_DONE_MARKER:-}" || -n "${G189_DONE_MARKER:-}" || -n "${G190_DONE_MARKER:-}" || -n "${G191_DONE_MARKER:-}" || -n "${G192_DONE_MARKER:-}" ]] && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			fi
			# New Game: FRAME_SAMPLE alone is not enough — wait for attack/jump/use
			# + sustained frames=32 (c3a2 was stopping mid-action).
			if (( DOLPHIN_NEWGAME )); then
				if probe_newgame_progress_ready; then
					if probe_guest_error; then DOLPHIN_EXIT=3; break; fi
					DOLPHIN_EXIT=0; break
				fi
				sleep 2
				continue
			fi
			if (( FRAME_SAMPLE_SEC <= 0 || $(date +%s) >= map_ready_at + FRAME_SAMPLE_SEC )); then
				DOLPHIN_EXIT=0; break
			fi
		elif probe_retail_menu_ready; then
			if probe_guest_error; then DOLPHIN_EXIT=3; break; fi
			if (( FRAME_SAMPLE_SEC <= 0 )); then
				DOLPHIN_EXIT=0; break
			fi
			if probe_log_has "$INPUT_MARKER"; then
				(( retail_ready_at == 0 )) && retail_ready_at=$(date +%s)
				if (( $(date +%s) >= retail_ready_at + FRAME_SAMPLE_SEC )); then
					DOLPHIN_EXIT=0; break
				fi
			fi
		elif probe_log_has "$GUEST_MARKER" && probe_guest_error; then
			DOLPHIN_EXIT=3; break
		fi
		sleep 2
	done
}

# Native dolphin-emu: same readiness/sample gate as Flatpak, then stop the process.
probe_wait_native() {
	"${DOLPHIN_CMD[@]}" >"$LOG_DIR/stdout.log" 2>"$LOG_DIR/stderr.log" &
	DOLPHIN_WRAPPER_PID=$!
	DOLPHIN_EXIT=124
	local deadline=$(( $(date +%s) + TIMEOUT_SEC ))
	local map_ready_at=0 retail_ready_at=0 g94_sample_armed=0 g278_sample_armed=0
	while (( $(date +%s) < deadline )); do
		if ! kill -0 "$DOLPHIN_WRAPPER_PID" 2>/dev/null; then
			wait "$DOLPHIN_WRAPPER_PID" >/dev/null 2>&1 || true
			DOLPHIN_EXIT=$?
			return
		fi
		if [[ -n "${G82_FAULT_MARKER:-}" ]] && probe_log_has "$G82_FAULT_MARKER"; then
			DOLPHIN_EXIT=0; break
		fi
		if probe_log_has "$MAP_MARKER" && probe_log_has "$INPUT_MARKER"; then
			if probe_newgame_progress_ready; then
				if probe_guest_error; then DOLPHIN_EXIT=3; break; fi
				DOLPHIN_EXIT=0; break
			fi
			if (( DOLPHIN_NEWGAME )); then
				# Landmark Flipper/G16x markers prove play progressed past
				# "play start ready" / frame-budget arm (often omitted on -gcnewgame).
				if [[ -z "${G159_DONE_MARKER:-}" && -z "${G161_DONE_MARKER:-}" && -z "${G162_DONE_MARKER:-}" && -z "${G163_DONE_MARKER:-}" && -z "${G164_DONE_MARKER:-}" && -z "${G165_DONE_MARKER:-}" && -z "${G166_DONE_MARKER:-}" && -z "${G167_DONE_MARKER:-}" && -z "${G168_DONE_MARKER:-}" && -z "${G169_DONE_MARKER:-}" && -z "${G170_DONE_MARKER:-}" && -z "${G171_DONE_MARKER:-}" && -z "${G172_DONE_MARKER:-}" && -z "${G173_DONE_MARKER:-}" && -z "${G174_DONE_MARKER:-}" && -z "${G175_DONE_MARKER:-}" && -z "${G176_DONE_MARKER:-}" && -z "${G177_DONE_MARKER:-}" && -z "${G178_DONE_MARKER:-}" && -z "${G179_DONE_MARKER:-}" && -z "${G180_DONE_MARKER:-}" && -z "${G181_DONE_MARKER:-}" && -z "${G182_DONE_MARKER:-}" && -z "${G183_DONE_MARKER:-}" && -z "${G184_DONE_MARKER:-}" && -z "${G185_DONE_MARKER:-}" && -z "${G186_DONE_MARKER:-}" && -z "${G187_DONE_MARKER:-}" && -z "${G188_DONE_MARKER:-}" && -z "${G189_DONE_MARKER:-}" && -z "${G190_DONE_MARKER:-}" && -z "${G191_DONE_MARKER:-}" && -z "${G192_DONE_MARKER:-}" ]]; then
					if ! probe_log_has "${PLAY_READY_MARKER:-Xash3D GameCube: play start ready}"; then
						sleep 2
						continue
					fi
					if [[ -n "${FRAME_ARMED_MARKER:-}" ]] && ! probe_log_has "$FRAME_ARMED_MARKER"; then
						sleep 2
						continue
					fi
				fi
			fi
			(( map_ready_at == 0 )) && map_ready_at=$(date +%s)
			if probe_guest_error; then DOLPHIN_EXIT=3; break; fi
			if [[ -n "${G94_DONE_MARKER:-}" ]] && ! probe_log_has "$G94_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G508_DONE_MARKER:-}" ]] && ! probe_log_has "$G508_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${TAS_DONE_MARKER:-}" ]] && ! probe_log_has "$TAS_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G68_DONE_MARKER:-}" ]] && ! probe_log_has "$G68_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G95_DONE_MARKER:-}" ]] && ! probe_log_has "$G95_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G96_DONE_MARKER:-}" ]]; then
				if ! probe_log_has "$G96_DONE_MARKER" \
					&& { [[ -z "${G96_ALT_MARKER:-}" ]] || ! probe_log_has "$G96_ALT_MARKER"; }; then
					sleep 2
					continue
				fi
			fi
			if [[ -n "${G97_DONE_MARKER:-}" ]] && ! probe_log_has "$G97_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G98_DONE_MARKER:-}" ]] && ! probe_log_has "$G98_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G99_DONE_MARKER:-}" ]] && ! probe_log_has "$G99_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G100_DONE_MARKER:-}" ]] && ! probe_log_has "$G100_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G102_DONE_MARKER:-}" ]] && ! probe_log_has "$G102_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G103_DONE_MARKER:-}" ]] && ! probe_log_has "$G103_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G104_DONE_MARKER:-}" ]] && ! probe_log_has "$G104_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G104_DEPLOY_MARKER:-}" ]] && ! probe_log_has "$G104_DEPLOY_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G105_DONE_MARKER:-}" ]] && ! probe_log_has "$G105_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G158_DONE_MARKER:-}" ]] && ! probe_log_has "$G158_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G159_DONE_MARKER:-}" ]] && ! probe_log_has "$G159_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G161_DONE_MARKER:-}" ]] && ! probe_log_has "$G161_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G162_DONE_MARKER:-}" ]] && ! probe_log_has "$G162_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G163_DONE_MARKER:-}" ]] && ! probe_log_has "$G163_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G164_DONE_MARKER:-}" ]] && ! probe_log_has "$G164_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G165_DONE_MARKER:-}" ]] && ! probe_log_has "$G165_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G166_DONE_MARKER:-}" ]] && ! probe_log_has "$G166_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G167_DONE_MARKER:-}" ]] && ! probe_log_has "$G167_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G168_DONE_MARKER:-}" ]] && ! probe_log_has "$G168_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G169_DONE_MARKER:-}" ]] && ! probe_log_has "$G169_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G170_DONE_MARKER:-}" ]] && ! probe_log_has "$G170_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G171_DONE_MARKER:-}" ]] && ! probe_log_has "$G171_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G172_DONE_MARKER:-}" ]] && ! probe_log_has "$G172_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G173: lean 320hud1 sheet real (not soft-fail stub).
			if [[ -n "${G173_DONE_MARKER:-}" ]] && ! probe_log_has "$G173_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G174: lean crosshairs sheet real (not soft-fail stub).
			if [[ -n "${G174_DONE_MARKER:-}" ]] && ! probe_log_has "$G174_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G175: outdoor Flipper refresh via 4x64 slots/cands trade.
			if [[ -n "${G175_DONE_MARKER:-}" ]] && ! probe_log_has "$G175_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G176: raised face cap via LM 8→4 trade.
			if [[ -n "${G176_DONE_MARKER:-}" ]] && ! probe_log_has "$G176_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G177: soft DumpFrames HUD composite (lean sheets).
			if [[ -n "${G177_DONE_MARKER:-}" ]] && ! probe_log_has "$G177_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G178_DONE_MARKER:-}" ]] && ! probe_log_has "$G178_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G179_DONE_MARKER:-}" ]] && ! probe_log_has "$G179_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G180_DONE_MARKER:-}" ]] && ! probe_log_has "$G180_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G181_DONE_MARKER:-}" ]] && ! probe_log_has "$G181_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G182_DONE_MARKER:-}" ]] && ! probe_log_has "$G182_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G183_DONE_MARKER:-}" ]] && ! probe_log_has "$G183_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G184_DONE_MARKER:-}" ]] && ! probe_log_has "$G184_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G185_DONE_MARKER:-}" ]] && ! probe_log_has "$G185_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G186_DONE_MARKER:-}" ]] && ! probe_log_has "$G186_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G187_DONE_MARKER:-}" ]] && ! probe_log_has "$G187_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G188_DONE_MARKER:-}" ]] && ! probe_log_has "$G188_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G189_DONE_MARKER:-}" ]] && ! probe_log_has "$G189_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G190_DONE_MARKER:-}" ]] && ! probe_log_has "$G190_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G191_DONE_MARKER:-}" ]] && ! probe_log_has "$G191_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G192_DONE_MARKER:-}" ]] && ! probe_log_has "$G192_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G193_DONE_MARKER:-}" ]] && ! probe_log_has "$G193_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G194_DONE_MARKER:-}" ]] && ! probe_log_has "$G194_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G195_DONE_MARKER:-}" ]] && ! probe_log_has "$G195_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G196_DONE_MARKER:-}" ]] && ! probe_log_has "$G196_DONE_MARKER"; then
				sleep 2
				continue
			fi
			# G278/G280: New Game Flipper present / intro markers.
			if [[ -n "${G278_DONE_MARKER:-}" ]] && ! probe_log_has "$G278_DONE_MARKER"; then
				sleep 2
				continue
			fi
			if [[ -n "${G278_DONE_MARKER:-}" ]] && probe_log_has "$G278_DONE_MARKER" && (( g278_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g278_sample_armed=1
				g94_sample_armed=1
			fi
			if [[ -n "${G101_DONE_MARKER:-}" ]]; then
				if ! probe_log_has "$G101_DONE_MARKER" \
					&& { [[ -z "${G101_ALT_MARKER:-}" ]] || ! probe_log_has "$G101_ALT_MARKER"; }; then
					sleep 2
					continue
				fi
			fi
			if [[ -n "${G94_DONE_MARKER:-}" ]] && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			fi
			if [[ -n "${G159_DONE_MARKER:-}" ]] && probe_log_has "$G159_DONE_MARKER" && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			elif [[ -n "${G158_DONE_MARKER:-}" ]] && probe_log_has "$G158_DONE_MARKER" && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			fi
			if [[ -n "${G68_DONE_MARKER:-}" || -n "${G95_DONE_MARKER:-}" || -n "${G96_DONE_MARKER:-}" || -n "${G97_DONE_MARKER:-}" || -n "${G98_DONE_MARKER:-}" || -n "${G99_DONE_MARKER:-}" || -n "${G100_DONE_MARKER:-}" || -n "${G101_DONE_MARKER:-}" || -n "${G102_DONE_MARKER:-}" || -n "${G103_DONE_MARKER:-}" || -n "${G104_DONE_MARKER:-}" || -n "${G105_DONE_MARKER:-}" || -n "${G158_DONE_MARKER:-}" || -n "${G159_DONE_MARKER:-}" || -n "${G161_DONE_MARKER:-}" || -n "${G162_DONE_MARKER:-}" || -n "${G163_DONE_MARKER:-}" || -n "${G164_DONE_MARKER:-}" || -n "${G165_DONE_MARKER:-}" || -n "${G166_DONE_MARKER:-}" || -n "${G167_DONE_MARKER:-}" || -n "${G168_DONE_MARKER:-}" || -n "${G169_DONE_MARKER:-}" || -n "${G170_DONE_MARKER:-}" || -n "${G171_DONE_MARKER:-}" || -n "${G172_DONE_MARKER:-}" || -n "${G173_DONE_MARKER:-}" || -n "${G174_DONE_MARKER:-}" || -n "${G175_DONE_MARKER:-}" || -n "${G176_DONE_MARKER:-}" || -n "${G177_DONE_MARKER:-}" || -n "${G178_DONE_MARKER:-}" || -n "${G179_DONE_MARKER:-}" || -n "${G180_DONE_MARKER:-}" || -n "${G181_DONE_MARKER:-}" || -n "${G182_DONE_MARKER:-}" || -n "${G183_DONE_MARKER:-}" || -n "${G184_DONE_MARKER:-}" || -n "${G185_DONE_MARKER:-}" || -n "${G186_DONE_MARKER:-}" || -n "${G187_DONE_MARKER:-}" || -n "${G188_DONE_MARKER:-}" || -n "${G189_DONE_MARKER:-}" || -n "${G190_DONE_MARKER:-}" || -n "${G191_DONE_MARKER:-}" || -n "${G192_DONE_MARKER:-}" ]] && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			fi
			# New Game: FRAME_SAMPLE alone is not enough — wait for attack/jump/use
			# + sustained frames=32 (c3a2 was stopping mid-action).
			if (( DOLPHIN_NEWGAME )); then
				if probe_newgame_progress_ready; then
					if probe_guest_error; then DOLPHIN_EXIT=3; break; fi
					DOLPHIN_EXIT=0; break
				fi
				sleep 2
				continue
			fi
			if (( FRAME_SAMPLE_SEC <= 0 || $(date +%s) >= map_ready_at + FRAME_SAMPLE_SEC )); then
				DOLPHIN_EXIT=0; break
			fi
		elif probe_retail_menu_ready; then
			if probe_guest_error; then DOLPHIN_EXIT=3; break; fi
			if (( FRAME_SAMPLE_SEC <= 0 )); then
				DOLPHIN_EXIT=0; break
			fi
			if probe_log_has "$INPUT_MARKER"; then
				(( retail_ready_at == 0 )) && retail_ready_at=$(date +%s)
				if (( $(date +%s) >= retail_ready_at + FRAME_SAMPLE_SEC )); then
					DOLPHIN_EXIT=0; break
				fi
			fi
		elif probe_log_has "$GUEST_MARKER" && probe_guest_error; then
			DOLPHIN_EXIT=3; break
		fi
		sleep 2
	done
	if kill -0 "$DOLPHIN_WRAPPER_PID" 2>/dev/null; then
		kill -TERM "$DOLPHIN_WRAPPER_PID" 2>/dev/null || true
		sleep 1
		kill -KILL "$DOLPHIN_WRAPPER_PID" 2>/dev/null || true
		wait "$DOLPHIN_WRAPPER_PID" >/dev/null 2>&1 || true
	fi
}

# Classify Dolphin exit status from probe logs and finalize.
probe_classify_results() {
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
	if [[ -n "${G508_DONE_MARKER:-}" ]] && ! probe_log_has "$G508_DONE_MARKER"; then
		echo "CHANGELEVEL_PARTIAL_READY: Destination and landmark restore markers were present, but G508 config round trip did not complete."
		echo "Logs: $LOG_DIR"
		finalize_probe changelevel_partial_ready 4
	fi
	if [[ -n "${TAS_DONE_MARKER:-}" ]] && ! probe_log_has "$TAS_DONE_MARKER"; then
		echo "CHANGELEVEL_PARTIAL_READY: Destination and landmark restore markers were present, but TAS replay did not complete."
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
	if (( NEWGAME_PROGRESS_FOUND )); then
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
}
