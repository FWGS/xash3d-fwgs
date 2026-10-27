#!/usr/bin/env bash
# Extract the portable Swiss stage tarball onto a mounted FAT volume.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

ARCHIVE="${SWISS_STAGE_TAR:-OUT/xash3d-gc-swiss-stage.tar.gz}"
MOUNT=""

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-swiss-extract-stage.sh --mount /path/to/sd

Extracts OUT/xash3d-gc-swiss-stage.tar.gz (apps/ + xash3d/) onto a mounted
FAT32 Swiss SD. Prefer this on a second PC that has the tarball but not the
full git tree / smoke image.

  --mount PATH   Mounted SD root (must be writable)
EOF
}

while (( $# > 0 )); do
	case "$1" in
		--mount) MOUNT="$2"; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

if [[ -z "$MOUNT" || ! -d "$MOUNT" || ! -w "$MOUNT" ]]; then
	echo "FAIL: need writable --mount PATH" >&2
	exit 2
fi
if [[ ! -f "$ARCHIVE" ]]; then
	echo "FAIL: missing $ARCHIVE — run scripts/gamecube-swiss-smoke-image.sh first" >&2
	exit 1
fi

echo "==> Extract $ARCHIVE → $MOUNT"
tar -C "$MOUNT" -xzf "$ARCHIVE"
sync
echo "DONE. Swiss → apps/xash3d-gc/boot.dol"
echo "After quit: scripts/gamecube-swiss-hw-complete.sh --log $MOUNT"
