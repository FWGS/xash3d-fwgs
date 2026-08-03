#!/usr/bin/env bash
set -Eeuo pipefail
REPO="$(git rev-parse --show-toplevel)"
cd "$REPO"
if (( $# < 2 || $# > 3 )); then
    printf 'usage: %s "hypothesis" file [second-file]\n' "$0" >&2
    exit 2
fi
HYPOTHESIS="$1"
shift
TARGETS=("$@")
BASELINE_PATH="$REPO/.ai/runtime-experiment-baseline.json"
EXPERIMENT_ID="$(date +%Y%m%d-%H%M%S)"
REPORT_DIR="$REPO/.ai/runtime-experiments/$EXPERIMENT_ID"
REPORT_PATH="$REPORT_DIR/report.json"
mkdir -p "$REPORT_DIR"
[[ -f "$BASELINE_PATH" ]] || { echo "Missing runtime baseline: $BASELINE_PATH" >&2; exit 2; }
for target in "${TARGETS[@]}"; do
    [[ -f "$target" ]] || { echo "Experiment target is not a file: $target" >&2; exit 2; }
done
mapfile -t DIRTY_PATHS < <(git status --porcelain --untracked-files=all | sed -E 's/^.. //; s/ -> .*//' | sed '/^$/d')
for dirty_path in "${DIRTY_PATHS[@]}"; do
    [[ "$dirty_path" == .ai/runtime-experiments/* ]] && continue
    allowed=0
    for target in "${TARGETS[@]}"; do
        [[ "$dirty_path" == "$target" ]] && allowed=1
    done
    if (( ! allowed )); then
        echo "Refusing experiment: unrelated dirty path: $dirty_path" >&2
        git status --short >&2
        exit 2
    fi
done
PARENT_COMMIT="$(git rev-parse HEAD)"
BASELINE_SIGNATURE="$(python3 - "$BASELINE_PATH" <<'PY'
import json, sys
print(json.load(open(sys.argv[1], encoding="utf-8"))["result_signature"])
PY
)"
set +e
scripts/build-gamecube.sh >"$REPORT_DIR/build.log" 2>&1
BUILD_STATUS=$?
if (( BUILD_STATUS == 0 )); then
    DOLPHIN_TIMEOUT="${DOLPHIN_TIMEOUT:-180}" scripts/dolphin-boot-probe.sh >"$REPORT_DIR/probe.log" 2>&1
    PROBE_STATUS=$?
else
    : >"$REPORT_DIR/probe.log"
    PROBE_STATUS=125
fi
set -e
if grep -qF "Xash3D GameCube: map loaded c0a0e" "$REPORT_DIR/probe.log"; then
    RESULT_SIGNATURE="PASS:map_loaded"
elif grep -qF "delta.lst" "$REPORT_DIR/probe.log"; then
    RESULT_SIGNATURE="GUEST_RUNTIME_ERROR:delta.lst"
elif grep -qF "MAP_TIMEOUT" "$REPORT_DIR/probe.log"; then
    RESULT_SIGNATURE="MAP_TIMEOUT"
else
    RESULT_SIGNATURE="exit:${PROBE_STATUS}"
fi
SOURCE_CHANGES="$(git status --porcelain -- engine ref common public re_agent)"
RECOMMENDATION="REVIEW_REQUIRED"
if (( BUILD_STATUS != 0 )) || [[ "$RESULT_SIGNATURE" == "$BASELINE_SIGNATURE" ]]; then
    RECOMMENDATION="REVERT_CANDIDATE"
elif [[ "$RESULT_SIGNATURE" == PASS:* ]]; then
    RECOMMENDATION="KEEP_CANDIDATE"
fi
python3 - "$REPORT_PATH" "$PARENT_COMMIT" "$HYPOTHESIS" "$BASELINE_SIGNATURE" "$RESULT_SIGNATURE" "$BUILD_STATUS" "$PROBE_STATUS" "$RECOMMENDATION" "$SOURCE_CHANGES" "${TARGETS[@]}" <<'PY'
import json
import sys
from pathlib import Path

out, parent, hypothesis, baseline, result, build, probe, recommendation, source_changes, *targets = sys.argv[1:]
payload = {
    "schema": "gamecube.runtime-experiment.v1",
    "experiment_id": Path(out).parent.name,
    "parent_commit": parent,
    "hypothesis": hypothesis,
    "targets": targets,
    "baseline_result": baseline,
    "result": result,
    "build": {"exit_code": int(build), "log": str(Path(out).parent / "build.log")},
    "probe": {"exit_code": int(probe), "log": str(Path(out).parent / "probe.log")},
    "changed_source": source_changes.splitlines() if source_changes else [],
    "decision": "PENDING_REVIEW",
    "recommendation": recommendation,
    "next_action": "Record KEEP or REVERT after reviewing logs; do not create a generic runtime-fix commit.",
}
Path(out).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
print(json.dumps(payload, indent=2))
PY
echo "Experiment report: $REPORT_PATH"
[[ "$RECOMMENDATION" != "REVERT_CANDIDATE" ]] || exit 10
(( BUILD_STATUS == 0 && PROBE_STATUS == 0 ))
