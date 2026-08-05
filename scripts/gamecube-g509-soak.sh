#!/usr/bin/env bash
# G509: bounded changelevel soak gate for the GameCube port.
#
# Default route is the proven early tram hop c0a0 → c0a0a. Use --dry-run to
# validate report generation without Dolphin.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

ITERATIONS="${G509_ITERATIONS:-2}"
TIMEOUT="${G509_TIMEOUT:-${SOAK_TIMEOUT:-180}}"
ROUTE="${G509_CHANGELEVEL_ROUTE:-c0a0:c0a0a}"
EXTRA_ARGS=()

while (($#)); do
	case "$1" in
		--dry-run)
			EXTRA_ARGS+=(--dry-run)
			shift
			;;
		--iterations)
			ITERATIONS="$2"
			shift 2
			;;
		--timeout)
			TIMEOUT="$2"
			shift 2
			;;
		--route)
			ROUTE="$2"
			shift 2
			;;
		--strict)
			EXTRA_ARGS+=(--strict)
			shift
			;;
		*)
			EXTRA_ARGS+=("$1")
			shift
			;;
	esac
done

exec python3 scripts/gamecube-soak-probe.py \
	--g509 \
	--changelevel-route "$ROUTE" \
	--iterations "$ITERATIONS" \
	--timeout "$TIMEOUT" \
	"${EXTRA_ARGS[@]}"
