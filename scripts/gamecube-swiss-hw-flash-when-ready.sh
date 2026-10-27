#!/usr/bin/env bash
# One-shot operator helper: stage or flash Swiss smoke media, then print ingest steps.
# Prefer --rsync (non-destructive) when the card is already mounted.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

MODE="rsync"
YES=0
DEVICE=""

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-swiss-hw-flash-when-ready.sh [--rsync|--dd] [--device /dev/sdX] [--yes]

Waits for Swiss SD media, deploys the current smoke packet, and prints the
post-console ingest command.

  --rsync          Wait for a new FAT mount and copy smoke stage (default, safe)
  --dd             Wait for a new USB/removable disk ≤512GiB and raw-write the img
                   (requires --yes; DESTROYS the device)
  --device PATH    With --dd: flash this disk immediately (no wait)
  --yes            Required for --dd

Env: SWISS_FLASH_DEVICE=/dev/sdX implies --dd --device … (still needs --yes)

List candidate disks without writing:
  scripts/gamecube-swiss-list-flash-targets.sh

Packet: OUT/xash3d-gc-swiss-smoke.img + .ai/logs/swiss-smoke-stage/
Also on Expansion: 01_GAME_DEV/xash3d-gc-swiss-hw-20260829/
EOF
}

while (( $# > 0 )); do
	case "$1" in
		--rsync) MODE=rsync ;;
		--dd) MODE=dd ;;
		--device) DEVICE="$2"; MODE=dd; shift ;;
		--yes) YES=1 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

if [[ -n "${SWISS_FLASH_DEVICE:-}" && -z "$DEVICE" ]]; then
	DEVICE="$SWISS_FLASH_DEVICE"
	MODE=dd
fi

if [[ ! -f OUT/bin/boot.dol ]]; then
	echo "FAIL: OUT/bin/boot.dol missing" >&2
	exit 1
fi
if [[ ! -d .ai/logs/swiss-smoke-stage/apps/xash3d-gc ]]; then
	echo "==> Building smoke stage/image"
	scripts/gamecube-swiss-smoke-image.sh --no-iso
fi

echo "==> Artifacts"
sha256sum OUT/bin/boot.dol OUT/xash3d-gc-swiss-smoke.img | sed 's/^/  /'

export SD_WAIT_TIMEOUT="${SD_WAIT_TIMEOUT:-1800}"

if [[ "$MODE" == "rsync" ]]; then
	echo "==> Waiting for SD mount (rsync, non-destructive)…"
	scripts/gamecube-swiss-sd-wait.sh --rsync
else
	if (( ! YES )); then
		echo "FAIL: --dd requires --yes (destroys the target disk)" >&2
		exit 2
	fi
	if [[ -n "$DEVICE" ]]; then
		echo "==> Flashing $DEVICE immediately (dd, DESTRUCTIVE)…"
		scripts/gamecube-swiss-sd-wait.sh --dd --device "$DEVICE" --yes
	else
		echo "==> Waiting for SD reader disk (dd, DESTRUCTIVE)…"
		scripts/gamecube-swiss-sd-wait.sh --dd --wait-device --yes
	fi
fi

cat <<'EOF'

== Next on GameCube ==
1. Insert card into SD2SP2 / SD Gecko
2. Swiss → apps/xash3d-gc/boot.dol
3. Expect splash → auto map c0a0e (gamecube.cfg + swissquit) → return to Swiss
   (backup: hold Z+Start to quit)

== Then on this PC ==
  scripts/gamecube-swiss-hw-complete.sh --log /run/media/tim/<sd-label>
  (oneshot does this automatically if still running)

EOF
