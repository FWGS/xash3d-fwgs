#!/usr/bin/env bash
# TODO-FS-022: summarize Swiss storage-route evidence and G508 round trips.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
LOG_ROOT="${FS022_LOG_ROOT:-.ai/logs}"
INPUT="${1:-}"

if [[ "${1:-}" == "--help" ]]; then
	cat <<'EOF'
Usage: scripts/gamecube-storage-route-matrix.sh [evidence-root]

evidence-root may be a probe directory, mounted Swiss volume, or directory
containing route subdirectories. Defaults to FS022_LOG_ROOT or .ai/logs.
EOF
	exit 0
fi

find_evidence() {
	local route="$1" file
	for file in "$INPUT/$route/swiss-evidence.txt" "$INPUT/$route/stderr.log" \
		"$INPUT/swiss-evidence.txt" "$INPUT/stderr.log"; do
		if [[ -f "$file" ]]; then
			printf '%s\n' "$file"
			return
		fi
	done
	# Prefer the newest matching evidence; directory traversal order is not
	# stable and can otherwise select an obsolete failed probe.
	find "$LOG_ROOT" -type f \( -name swiss-evidence.txt -o -name stderr.log \) \
		-printf '%T@ %p\n' 2>/dev/null | sort -nr \
		| while read -r _ file; do
			if grep -aqE "${route}:/|route=${route}" "$file"; then
				printf '%s\n' "$file"
				break
			fi
		done
}

printf 'route\tevidence\tg508\tstatus\n'
printf '%s\n' '-----'
for route in sd carda cardb disc; do
	file="$(find_evidence "$route" | head -n 1 || true)"
	if [[ -z "$file" ]]; then
		printf '%s\t%s\t%s\t%s\n' "$route" 'missing' 'unseen' 'FAIL'
		continue
	fi
	if [[ "$route" == disc ]]; then
		marker='G508 config round trip ready'
	else
		marker="G508 config round trip ready route=${route}"
	fi
	if grep -aqF "$marker" "$file"; then
		printf '%s\t%s\t%s\t%s\n' "$route" "$file" 'PASS' 'PASS'
	else
		printf '%s\t%s\t%s\t%s\n' "$route" "$file" 'FAIL' 'FAIL'
	fi
done
