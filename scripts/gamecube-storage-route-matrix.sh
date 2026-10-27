#!/usr/bin/env bash
# TODO-FS-022: summarize Swiss storage-route evidence and G508 round trips.
# TODO-FS-025: ingest gc-fs-route-v1 schema records (fs-route.json).
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
	cat <<'EOF'
Usage: scripts/gamecube-storage-route-matrix.sh [evidence-root]

evidence-root may be a probe directory, mounted Swiss volume, or directory
containing route subdirectories (sd/, carda/, cardb/, disc/, hybrid/) with
swiss-evidence.txt, stderr.log, and/or fs-route.json.

Environment:
  FS022_LOG_ROOT   Search root for newest matching evidence (default .ai/logs)
  FS022_OUT        Output directory for summary.md + matrix.tsv
  FS022_STRICT=1   Exit 1 unless FS022_VERIFY: PASS
EOF
	exit 0
fi

INPUT="${1:-}"
ARGS=()
if [[ -n "$INPUT" ]]; then
	ARGS+=("$INPUT")
fi
if [[ -n "${FS022_LOG_ROOT:-}" ]]; then
	ARGS+=(--log-root "$FS022_LOG_ROOT")
fi
if [[ -n "${FS022_OUT:-}" ]]; then
	ARGS+=(--out "$FS022_OUT")
fi
if [[ "${FS022_STRICT:-}" == "1" || "${FS022_STRICT:-}" == "true" ]]; then
	ARGS+=(--strict)
fi

exec python3 scripts/waifulib/gamecube_fs_route.py "${ARGS[@]}"
