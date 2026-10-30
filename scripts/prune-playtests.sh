#!/usr/bin/env bash
# Prune .ai/playtests: keep the newest N recordings (default 20) and every
# recording whose sidecar JSON has "keep": true. Lists what would go and
# deletes only with --yes.
#
#   scripts/prune-playtests.sh            # dry run, keep newest 20
#   scripts/prune-playtests.sh 10 --yes   # keep newest 10, delete the rest
#   scripts/playtest-record.py annotate .ai/playtests/<rec>.json keep=true
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/.ai/playtests"
KEEP=20
YES=0
for a in "$@"; do
	case "$a" in
		--yes) YES=1 ;;
		-h|--help) sed -n 2,9p "$0"; exit 0 ;;
		*) KEEP="$a" ;;
	esac
done
[[ -d "$DIR" ]] || { echo "no $DIR"; exit 0; }

mapfile -t victims < <(python3 - "$DIR" "$KEEP" <<'PY'
import json, os, sys
d, keep = sys.argv[1], int(sys.argv[2])
recs = sorted((n for n in os.listdir(d) if n.endswith(".json")), reverse=True)
for i, n in enumerate(recs):
	try:
		meta = json.load(open(os.path.join(d, n)))
	except (OSError, ValueError):
		continue
	if i < keep or meta.get("keep"):
		continue
	base = n[:-5]
	for ext in (".json", ".mp4", ".mkv", ".ffmpeg.log"):
		if os.path.exists(os.path.join(d, base + ext)):
			print(os.path.join(d, base + ext))
PY
)

if (( ${#victims[@]} == 0 )); then
	echo "nothing to prune (keeping newest $KEEP and keep=true)"
	exit 0
fi
printf '%s\n' "${victims[@]}"
if (( YES )); then
	rm -f -- "${victims[@]}"
	python3 "$ROOT/scripts/playtest-record.py" index
	echo "deleted ${#victims[@]} files"
else
	echo "(dry run: ${#victims[@]} files; rerun with --yes to delete)"
fi
