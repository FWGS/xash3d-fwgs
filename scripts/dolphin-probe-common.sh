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
				if ! probe_log_has "${PLAY_READY_MARKER:-Xash3D GameCube: play start ready}"; then
					sleep 2
					continue
				fi
				if [[ -n "${FRAME_ARMED_MARKER:-}" ]] && ! probe_log_has "$FRAME_ARMED_MARKER"; then
					sleep 2
					continue
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
			# Once G94 restore present is seen, restart the sample window.
			if [[ -n "${G94_DONE_MARKER:-}" ]] && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			fi
			if [[ -n "${G68_DONE_MARKER:-}" || -n "${G95_DONE_MARKER:-}" || -n "${G96_DONE_MARKER:-}" || -n "${G97_DONE_MARKER:-}" || -n "${G98_DONE_MARKER:-}" ]] && (( g94_sample_armed == 0 )); then
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
				if ! probe_log_has "${PLAY_READY_MARKER:-Xash3D GameCube: play start ready}"; then
					sleep 2
					continue
				fi
				if [[ -n "${FRAME_ARMED_MARKER:-}" ]] && ! probe_log_has "$FRAME_ARMED_MARKER"; then
					sleep 2
					continue
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
			if [[ -n "${G94_DONE_MARKER:-}" ]] && (( g94_sample_armed == 0 )); then
				map_ready_at=$(date +%s)
				g94_sample_armed=1
			fi
			if [[ -n "${G68_DONE_MARKER:-}" || -n "${G95_DONE_MARKER:-}" || -n "${G96_DONE_MARKER:-}" || -n "${G97_DONE_MARKER:-}" || -n "${G98_DONE_MARKER:-}" ]] && (( g94_sample_armed == 0 )); then
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
