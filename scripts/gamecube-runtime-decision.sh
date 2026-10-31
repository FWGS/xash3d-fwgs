#!/usr/bin/env bash
set -Eeuo pipefail
if (( $# != 2 )); then
    printf 'usage: %s report.json KEEP|REVERT\n' "$0" >&2
    exit 2
fi
REPORT="$1"
DECISION="${2^^}"
[[ "$DECISION" == KEEP || "$DECISION" == REVERT ]] || { echo "Decision must be KEEP or REVERT." >&2; exit 2; }
[[ -f "$REPORT" ]] || { echo "Report not found: $REPORT" >&2; exit 2; }
if [[ "$DECISION" == KEEP ]]; then
    python3 - "$REPORT" <<'PY'
import json, sys
from pathlib import Path
path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
if data.get("recommendation") != "KEEP_CANDIDATE":
    raise SystemExit("Refusing KEEP: experiment did not produce a passing probe result.")
data["decision"] = "KEEP"
data["decision_note"] = "Kept explicitly after reviewing build and probe logs."
path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
PY
    echo "Recorded KEEP: review the diff, then create a specific commit manually."
    exit 0
fi
python3 - "$REPORT" <<'PY'
import json, subprocess, sys
from pathlib import Path
path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
parent = data.get("parent_commit")
current = subprocess.run(["git", "rev-parse", "HEAD"], text=True, capture_output=True, check=True).stdout.strip()
if current != parent:
    raise SystemExit("Refusing REVERT: HEAD moved since the experiment started.")
targets = data.get("targets") or []
if not targets or len(targets) > 2:
    raise SystemExit("Refusing REVERT: report has no bounded target list.")
subprocess.run(["git", "restore", "--worktree", "--staged", "--", *targets], check=True)
data["decision"] = "REVERT"
data["decision_note"] = "Targeted experiment files restored explicitly after reviewing build and probe logs."
path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
PY
echo "Recorded REVERT and restored only the experiment target files."
