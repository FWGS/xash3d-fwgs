#!/usr/bin/env bash
# Release-candidate evidence gate for the native GameCube port.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT" || exit 1

STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="${RC_LOG_DIR:-.ai/logs/rc-check-$STAMP}"
SUMMARY="$LOG_DIR/summary.md"
STATUS_JSON="$LOG_DIR/status.json"
MANIFEST="$LOG_DIR/artifact-manifest.tsv"
MAP_LIST="${RC_MAP_LIST:-c0a0e}"
BOOT_TIMEOUT="${RC_BOOT_TIMEOUT:-180}"
MAP_TIMEOUT="${RC_MAP_TIMEOUT:-120}"
TARGET_FRAME_TIME="${TARGET_FRAME_TIME:-16.67}"
STRICT_COMPLIANCE="${RC_STRICT_COMPLIANCE:-0}"
DOLPHIN_RETRIES="${RC_DOLPHIN_RETRIES:-2}"
COMMAND_LINE="scripts/gamecube-rc-check.sh $*"

mkdir -p "$LOG_DIR"

PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0
declare -a RESULTS

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-rc-check.sh

Runs the native GameCube release-candidate evidence gate:
  1. ai-verify
  2. clean GameCube build
  3. artifact manifest
  4. local content staging audit when legal Half-Life assets are present
  5. optional smoke disc build when RC_BUILD_DISC=1
  6. Dolphin boot probe
  7. frame-budget probe
  8. map compatibility summary
  9. boot media compliance check
  10. video compliance check
  11. controller compliance check
  12. save integrity/destructive-action compliance check
  13. fatal error UX compliance check
  14. homebrew compliance check

Useful environment knobs:
  RC_LOG_DIR              Override output log directory.
  RC_MAP_LIST             Space-separated maps for map compatibility probe.
  RC_SMOKE_MAP            Smoke map for content/disc validation.
  RC_BOOT_TIMEOUT         Dolphin boot timeout seconds.
  RC_MAP_TIMEOUT          Map compatibility timeout seconds.
  RC_DOLPHIN_RETRIES      Bounded retries for Dolphin-dependent gates.
  RC_BUILD_DISC=1         Build OUT/xash3d-gc.iso if legal assets are present.
  RC_STRICT_COMPLIANCE=1  Run strict compliance mode near release/hardware gates.

This command does not prove real hardware behavior. G38/G53/G66 must carry
physical GameCube, Swiss, or compatible Wii/GameCube-mode evidence.
EOF
}

case "${1:-}" in
	-h|--help)
		usage
		exit 0
		;;
esac

log_status() {
	local name="$1"
	local status="$2"
	local log_path="$3"
	local note="$4"
	RESULTS+=("$name	$status	$log_path	$note")
	case "$status" in
		PASS) PASS_COUNT=$((PASS_COUNT + 1)) ;;
		WARN) WARN_COUNT=$((WARN_COUNT + 1)) ;;
		*) FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
	esac
}

run_gate() {
	local name="$1"
	local log_name="$2"
	shift 2
	local log_path="$LOG_DIR/$log_name"
	echo
	echo "== $name =="
	echo "\$ $*"
	if "$@" >"$log_path" 2>&1; then
		log_status "$name" "PASS" "$log_path" "command succeeded"
		tail -40 "$log_path"
		return 0
	fi
	local rc=$?
	log_status "save compliance" "FAIL" "$log_path" "exit $rc"
	echo "FAIL: $name (exit $rc)" >&2
	tail -80 "$log_path" >&2
	return "$rc"
}

hash_file() {
	local path="$1"
	if [[ -s "$path" ]]; then
		sha256sum "$path" | awk '{print $1}'
	else
		printf 'missing'
	fi
}

write_manifest() {
	{
		printf "path\tsize\tsha256\n"
		for path in \
			OUT/bin/boot.dol \
			OUT/bin/xash \
			OUT/xash3d-gc.iso \
			OUT/libref_gx.a \
			OUT/libfilesystem_stdio.a \
			OUT/valve/extras.pk3
		do
			if [[ -e "$path" ]]; then
				printf "%s\t%s\t%s\n" "$path" "$(stat -c '%s' "$path")" "$(hash_file "$path")"
			else
				printf "%s\tmissing\tmissing\n" "$path"
			fi
		done
	} >"$MANIFEST"
}

content_staging_audit() {
	local log_path="$LOG_DIR/content-staging-audit.log"
	echo
	echo "== content staging audit =="
	if [[ ! -d Half-Life/valve ]]; then
		echo "WARN: Half-Life/valve is missing; full local asset staging audit skipped" | tee "$log_path"
		log_status "content staging audit" "WARN" "$log_path" "Half-Life/valve missing"
		return 0
	fi
if RC_SMOKE_MAP="${RC_SMOKE_MAP:-c0a0e}" python3 - <<'PY' >"$log_path" 2>&1
import importlib.util
import sys
import os
import tempfile
from pathlib import Path
spec = importlib.util.spec_from_file_location("disc", "scripts/build-gamecube-disc.py")
disc = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = disc
spec.loader.exec_module(disc)
source = Path("Half-Life/valve")
smoke_map = os.environ.get("RC_SMOKE_MAP", "c0a0e")
with tempfile.TemporaryDirectory(prefix="xash3d-gc-rc-stage-") as temp:
    staged = disc.stage_smoke_data(source, Path(temp) / "valve", smoke_map)
    errors = disc.validate_smoke_assets(staged, smoke_map)
    if errors:
        print("Staged asset validation failed:")
        for error in errors:
            print(f"- {error}")
        raise SystemExit(1)
    staged_files = sum(1 for path in staged.rglob("*") if path.is_file())
    staged_bytes = sum(path.stat().st_size for path in staged.rglob("*") if path.is_file())
    print(f"Staged asset validation passed for map {smoke_map}.")
    print(f"Staged files: {staged_files}")
    print(f"Staged bytes: {staged_bytes}")
PY
	then
		log_status "content staging audit" "PASS" "$log_path" "staged smoke content passed critical/case/size checks"
		cat "$log_path"
		return 0
	fi
	local rc=$?
	log_status "content staging audit" "FAIL" "$log_path" "asset validation failed"
	cat "$log_path" >&2
	return "$rc"
}

build_disc_gate() {
	local log_path="$LOG_DIR/build-disc.log"
	echo
	echo "== disc image build =="
	if [[ ! -d Half-Life/valve ]]; then
		echo "WARN: Half-Life/valve is missing; disc image build skipped" | tee "$log_path"
		log_status "disc image build" "WARN" "$log_path" "Half-Life/valve missing"
		return 0
	fi
	if scripts/build-gamecube-disc.py --smoke-map "${RC_SMOKE_MAP:-c0a0e}" >"$log_path" 2>&1; then
		log_status "disc image build" "PASS" "$log_path" "OUT/xash3d-gc.iso built"
		tail -40 "$log_path"
		return 0
	fi
	local rc=$?
	log_status "disc image build" "FAIL" "$log_path" "exit $rc"
	tail -80 "$log_path" >&2
	return "$rc"
}

dolphin_boot_gate() {
	local log_path="$LOG_DIR/dolphin-boot-probe.log"
	local attempt_log
	local attempt
	echo
	echo "== Dolphin boot probe =="
	: >"$log_path"
	for attempt in $(seq 1 "$DOLPHIN_RETRIES"); do
		attempt_log="$LOG_DIR/dolphin-boot-probe-attempt-${attempt}.log"
		echo "Attempt $attempt/$DOLPHIN_RETRIES" | tee -a "$log_path"
		if DOLPHIN_TIMEOUT="$BOOT_TIMEOUT" scripts/dolphin-boot-probe.sh >"$attempt_log" 2>&1; then
			cat "$attempt_log" >>"$log_path"
			log_status "Dolphin boot probe" "PASS" "$log_path" "boot probe reached acceptance on attempt $attempt"
			tail -80 "$log_path"
			return 0
		fi
		local rc=$?
		cat "$attempt_log" >>"$log_path"
		echo "Attempt $attempt failed with exit $rc" >>"$log_path"
	done
	log_status "Dolphin boot probe" "FAIL" "$log_path" "all $DOLPHIN_RETRIES attempt(s) failed"
	tail -120 "$log_path" >&2
	return 1
}

frame_budget_gate() {
	local log_path="$LOG_DIR/frame-budget-probe.log"
	local attempt_log
	local attempt
	echo
	echo "== frame-budget probe =="
	: >"$log_path"
	for attempt in $(seq 1 "$DOLPHIN_RETRIES"); do
		attempt_log="$LOG_DIR/frame-budget-probe-attempt-${attempt}.log"
		echo "Attempt $attempt/$DOLPHIN_RETRIES" | tee -a "$log_path"
		if DOLPHIN_TIMEOUT="$BOOT_TIMEOUT" scripts/dolphin-boot-probe.sh >"$attempt_log" 2>&1; then
			cat "$attempt_log" >>"$log_path"
			if grep -Eq "G36_STATUS: (PASS|WEAK)|FRAME_BUDGET_STATS: samples=[1-9]" "$attempt_log"; then
				log_status "frame-budget probe" "PASS" "$log_path" "frame-budget telemetry present on attempt $attempt"
				grep -E "G36_STATUS|G36_SUMMARY|FRAME_BUDGET_STATS" "$attempt_log" | tail -20
				return 0
			fi
			echo "Attempt $attempt succeeded but had no frame-budget telemetry" >>"$log_path"
		else
			local rc=$?
			cat "$attempt_log" >>"$log_path"
			echo "Attempt $attempt failed with exit $rc" >>"$log_path"
		fi
	done
	log_status "frame-budget probe" "FAIL" "$log_path" "missing frame-budget telemetry after $DOLPHIN_RETRIES attempt(s)"
	grep -E "G36_STATUS|G36_SUMMARY|FRAME_BUDGET|MAP_READY|VISUAL" "$log_path" | tail -100 >&2
	return 1
}

map_compat_gate() {
	local log_path="$LOG_DIR/map-compat.log"
	echo
	echo "== map compatibility summary =="
	if [[ ! -d Half-Life/valve/maps ]]; then
		echo "WARN: Half-Life/valve/maps is missing; map compatibility skipped" | tee "$log_path"
		log_status "map compatibility summary" "WARN" "$log_path" "Half-Life/valve/maps missing"
		return 0
	fi
	if MAP_COMPAT_TIMEOUT="$MAP_TIMEOUT" scripts/gamecube-map-compat-probe.sh $MAP_LIST >"$log_path" 2>&1; then
		local latest
		latest="$(ls -td .ai/logs/map-compat-* 2>/dev/null | head -n 1 || true)"
		if [[ -n "$latest" && -s "$latest/summary.md" ]]; then
			cp "$latest/summary.md" "$LOG_DIR/map-compat-summary.md"
			cp "$latest/results.tsv" "$LOG_DIR/map-compat-results.tsv" 2>/dev/null || true
		fi
		if [[ -n "$latest" && -s "$latest/results.tsv" ]] && \
			awk -F '\t' 'NR > 1 && $2 !~ /^(MAP_LOADED|MAP_READY)$/ { bad=1 } END { exit bad ? 0 : 1 }' "$latest/results.tsv"; then
			log_status "map compatibility summary" "FAIL" "$log_path" "${latest}: one or more maps did not load"
			tail -80 "$log_path" >&2
			return 1
		fi
		log_status "map compatibility summary" "PASS" "$log_path" "${latest:-summary generated}"
		tail -60 "$log_path"
		return 0
	fi
	local rc=$?
	log_status "map compatibility summary" "FAIL" "$log_path" "exit $rc"
	tail -100 "$log_path" >&2
	return "$rc"
}

boot_media_compliance_gate() {
	local log_path="$LOG_DIR/boot-media-compliance.log"
	echo
	echo "== boot media compliance =="
	local args=(scripts/gamecube-boot-media-compliance.py --smoke-map "${RC_SMOKE_MAP:-c0a0e}" --log-dir "$LOG_DIR/boot-media-compliance")
	if [[ "${RC_BUILD_DISC:-0}" == "1" ]]; then
		args+=(--build-disc)
	fi
	if "${args[@]}" >"$log_path" 2>&1; then
		log_status "boot media compliance" "PASS" "$log_path" "G43 missing/case/corrupt-media diagnostics passed"
		cat "$log_path"
		return 0
	fi
	local rc=$?
	log_status "boot media compliance" "FAIL" "$log_path" "exit $rc"
	cat "$log_path" >&2
	return "$rc"
}

video_compliance_gate() {
	local log_path="$LOG_DIR/video-compliance.log"
	echo
	echo "== video compliance =="
	if scripts/gamecube-video-compliance.py --log-dir "$LOG_DIR/video-compliance" >"$log_path" 2>&1; then
		log_status "video compliance" "PASS" "$log_path" "G44 video mode/safe-area preflight passed"
		cat "$log_path"
		return 0
	fi
	local rc=$?
	log_status "video compliance" "FAIL" "$log_path" "exit $rc"
	cat "$log_path" >&2
	return "$rc"
}

controller_compliance_gate() {
	local log_path="$LOG_DIR/controller-compliance.log"
	echo
	echo "== controller compliance =="
	if scripts/gamecube-controller-compliance.py --log-dir "$LOG_DIR/controller-compliance" >"$log_path" 2>&1; then
		log_status "controller compliance" "PASS" "$log_path" "G45 controller source/policy preflight passed"
		cat "$log_path"
		return 0
	fi
	local rc=$?
	log_status "controller compliance" "FAIL" "$log_path" "exit $rc"
	cat "$log_path" >&2
	return "$rc"
}

save_compliance_gate() {
	local log_path="$LOG_DIR/save-compliance.log"
	echo
	echo "== save compliance =="
	if scripts/gamecube-save-compliance.py --log-dir "$LOG_DIR/save-compliance" >"$log_path" 2>&1; then
		log_status "save compliance" "PASS" "$log_path" "G46 save integrity/destructive-action preflight passed"
		cat "$log_path"
		return 0
	fi
	local rc=$?
	log_status "save compliance" "FAIL" "$log_path" "exit $rc"
	cat "$log_path" >&2
	return "$rc"
}

fatal_ux_compliance_gate() {
	local log_path="$LOG_DIR/fatal-ux-compliance.log"
	echo
	echo "== fatal UX compliance =="
	if scripts/gamecube-fatal-ux-compliance.py --log-dir "$LOG_DIR/fatal-ux-compliance" >"$log_path" 2>&1; then
		log_status "fatal UX compliance" "PASS" "$log_path" "G50 fatal error UX/crash breadcrumb preflight passed"
		cat "$log_path"
		return 0
	fi
	local rc=$?
	log_status "fatal UX compliance" "FAIL" "$log_path" "exit $rc"
	cat "$log_path" >&2
	return "$rc"
}

compliance_gate() {
	local log_path="$LOG_DIR/compliance.log"
	echo
	echo "== homebrew compliance =="
	local args=(scripts/gamecube-homebrew-compliance-check.py)
	if [[ "$STRICT_COMPLIANCE" == "1" ]]; then
		args+=(--strict)
	fi
	if "${args[@]}" >"$log_path" 2>&1; then
		log_status "homebrew compliance" "PASS" "$log_path" "compliance checks passed"
		cat "$log_path"
		return 0
	fi
	local rc=$?
	log_status "homebrew compliance" "FAIL" "$log_path" "exit $rc"
	cat "$log_path" >&2
	return "$rc"
}

write_summary() {
	write_manifest
	local commit
	local dirty=""
	local gcc_version="missing"
	local devkitpro="${DEVKITPRO:-/opt/devkitpro}"
	local devkitppc="${DEVKITPPC:-$devkitpro/devkitPPC}"
	commit="$(git rev-parse --short HEAD)"
	if ! git diff --quiet || ! git diff --cached --quiet; then
		dirty="-dirty"
	fi
	if [[ -x "$devkitppc/bin/powerpc-eabi-gcc" ]]; then
		gcc_version="$("$devkitppc/bin/powerpc-eabi-gcc" --version | head -n 1)"
	fi
	{
		echo "# GameCube Release Candidate Check"
		echo
		echo "- Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
		echo "- Commit: ${commit}${dirty}"
		echo "- Command: \`$COMMAND_LINE\`"
		echo "- Log directory: \`$LOG_DIR\`"
		echo "- DEVKITPRO: \`$devkitpro\`"
		echo "- DEVKITPPC: \`$devkitppc\`"
		echo "- powerpc-eabi-gcc: \`$gcc_version\`"
		echo "- RC_MAP_LIST: \`$MAP_LIST\`"
		echo "- RC_SMOKE_MAP: \`${RC_SMOKE_MAP:-c0a0e}\`"
		echo "- RC_BUILD_DISC: \`${RC_BUILD_DISC:-0}\`"
		echo "- RC_STRICT_COMPLIANCE: \`$STRICT_COMPLIANCE\`"
		echo "- RC_DOLPHIN_RETRIES: \`$DOLPHIN_RETRIES\`"
		echo "- Pass: $PASS_COUNT"
		echo "- Warn: $WARN_COUNT"
		echo "- Fail: $FAIL_COUNT"
		echo
		echo "## Gates"
		echo
		echo "| Gate | Status | Log | Note |"
		echo "|---|---|---|---|"
		for row in "${RESULTS[@]}"; do
			IFS=$'\t' read -r name status log_path note <<<"$row"
			echo "| $name | $status | \`$log_path\` | $note |"
		done
		echo
		echo "## Artifact Manifest"
		echo
		echo "\`$MANIFEST\`"
		echo
		echo '```tsv'
		cat "$MANIFEST"
		echo '```'
		echo
		echo "## Evidence Boundary"
		echo
		echo "This suite is the canonical local RC gate for source, build, artifact,"
		echo "Dolphin, map, frame-budget, staging, and compliance evidence. It does"
		echo "not close real hardware gates; G38, G53, and G66 still require dated"
		echo "physical GameCube, Swiss, or compatible Wii/GameCube-mode evidence."
	} >"$SUMMARY"

	python3 - "$STATUS_JSON" "$LOG_DIR" "$PASS_COUNT" "$WARN_COUNT" "$FAIL_COUNT" <<'PY'
import json
import sys
from pathlib import Path
path, log_dir, passed, warned, failed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
Path(path).write_text(json.dumps({
    "log_dir": log_dir,
    "passed": passed,
    "warned": warned,
    "failed": failed,
    "ok": failed == 0,
}, indent=2) + "\n", encoding="utf-8")
PY
}

main() {
	echo "GameCube RC check logs: $LOG_DIR"
	run_gate "ai-verify" "ai-verify.log" scripts/ai-verify.sh || true
	run_gate "clean build" "build-gamecube.log" scripts/build-gamecube.sh || true
	write_manifest
	content_staging_audit || true
	if [[ "${RC_BUILD_DISC:-0}" == "1" ]]; then
		build_disc_gate || true
	fi
	dolphin_boot_gate || true
	frame_budget_gate || true
	map_compat_gate || true
	boot_media_compliance_gate || true
	video_compliance_gate || true
	controller_compliance_gate || true
	save_compliance_gate || true
	fatal_ux_compliance_gate || true
	compliance_gate || true
	write_summary
	echo
	echo "RC summary: $SUMMARY"
	if (( FAIL_COUNT > 0 )); then
		echo "RC check failed: $FAIL_COUNT gate(s) failed, $WARN_COUNT warning(s)." >&2
		exit 1
	fi
	if (( WARN_COUNT > 0 )); then
		echo "RC check passed with $WARN_COUNT warning(s)."
	else
		echo "RC check passed."
	fi
}

main "$@"
