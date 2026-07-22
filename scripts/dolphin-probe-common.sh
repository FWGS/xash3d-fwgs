# Shared helpers for dolphin-boot-probe.sh
# shellcheck shell=bash

probe_log_has() {
	local needle="$1"
	grep -aqsF "$needle" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null
}

probe_guest_error() {
	grep -aEiq 'Host_Error|Sys_Error|Xash Error|_Mem_Alloc: out of memory|fatal error|guest.*(crash|abort)|Invalid read from|MMU fault|Program attempting to read|trashed (small )?header sentinel' \
		"$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null
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

probe_wait_flatpak() {
	flatpak kill "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1 || true
	trap cleanup_flatpak_dolphin EXIT
	"${DOLPHIN_CMD[@]}" >"$LOG_DIR/stdout.log" 2>"$LOG_DIR/stderr.log" &
	DOLPHIN_WRAPPER_PID=$!
	DOLPHIN_EXIT=124
	local deadline=$(( $(date +%s) + TIMEOUT_SEC ))
	local map_ready_at=0 retail_ready_at=0 g94_sample_armed=0
	while (( $(date +%s) < deadline )); do
		if [[ -n "${G82_FAULT_MARKER:-}" ]] && probe_log_has "$G82_FAULT_MARKER"; then
			DOLPHIN_EXIT=0; break
		fi
		if probe_log_has "$MAP_MARKER" && probe_log_has "$INPUT_MARKER"; then
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
	local map_ready_at=0 retail_ready_at=0 g94_sample_armed=0
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
