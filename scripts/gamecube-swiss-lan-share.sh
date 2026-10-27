#!/usr/bin/env bash
# Serve Swiss HW flash packet on the LAN for off-host phones/laptops.
# Does not flash or invent evidence — only hosts ready artifacts.
#
#   scripts/gamecube-swiss-lan-share.sh              # prepare + serve (foreground-friendly nohup)
#   scripts/gamecube-swiss-lan-share.sh --prepare-only
#   systemctl --user start xash3d-gc-swiss-lan-share.service
set -euo pipefail
ROOT="$(git -C "${0%/*}/.." rev-parse --show-toplevel 2>/dev/null || echo "$HOME/Desktop/xash3d-gc")"
cd "$ROOT"

PREPARE_ONLY=0
if [[ "${1:-}" == "--prepare-only" ]]; then
	PREPARE_ONLY=1
fi

PORT="${SWISS_LAN_SHARE_PORT:-8765}"
DIR="${SWISS_LAN_SHARE_DIR:-.ai/logs/swiss-lan-share}"
PIDFILE="$DIR/http.pid"
LOG="$DIR/http.log"

mkdir -p "$DIR"

# Prefer OUT/ (canonical); Desktop copies override when present.
link_pref() {
	local name="$1"; shift
	local src
	for src in "$@"; do
		if [[ -f "$src" ]]; then
			ln -sfn "$(realpath "$src")" "$DIR/$name"
			return 0
		fi
	done
	return 1
}

link_pref "xash3d-gc-swiss-smoke.img" \
	"$HOME/Desktop/xash3d-gc-swiss-smoke.img" \
	"OUT/xash3d-gc-swiss-smoke.img"
link_pref "boot.dol" \
	"$HOME/Desktop/xash3d-gc-swiss-boot.dol" \
	"OUT/bin/boot.dol"
link_pref "xash3d-gc-swiss-stage.tar.gz" \
	"$HOME/Desktop/xash3d-gc-swiss-stage.tar.gz"
link_pref "xash3d-gc-swiss-disc.iso" \
	"OUT/xash3d-gc-swiss-disc.iso"
link_pref "OFFHOST.txt" \
	"$HOME/Desktop/XASH3D-GC-OFFHOST.txt" \
	"/run/media/tim/Expansion/01_GAME_DEV/xash3d-gc-swiss-hw-20260829/OFFHOST.txt"

if [[ ! -e "$DIR/xash3d-gc-swiss-smoke.img" || ! -e "$DIR/boot.dol" ]]; then
	echo "FAIL: missing smoke.img or boot.dol for LAN share" >&2
	ls -la "$DIR" >&2 || true
	exit 1
fi

{
	echo "Xash3D GC Swiss LAN share — $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo
	sha256sum "$DIR"/xash3d-gc-swiss-smoke.img "$DIR"/boot.dol 2>/dev/null || true
	echo
	echo "Flash the .img to a FAT32 SD (destroys card), boot Swiss → apps/xash3d-gc/boot.dol,"
	echo "then copy xash3d/valve/logs/swiss-evidence.txt back to ceres Desktop as"
	echo "XASH3D-GC-SWISS-EVIDENCE.txt"
	echo
	echo "URLs:"
	ip -4 -br addr show scope global 2>/dev/null | awk '/UP|UNKNOWN/ {print $3}' | while read -r cidr; do
		ip="${cidr%%/*}"
		case "$ip" in
			127.*|172.1[6-9].*|172.2[0-9].*|172.3[0-1].*) continue ;;
		esac
		echo "  http://${ip}:${PORT}/"
	done
	echo
	ls -lah "$DIR" | sed 's/^/  /'
} >"$DIR/README.txt"

print_urls() {
	echo "SWISS_LAN_SHARE port=$PORT dir=$DIR"
	ip -4 -br addr show scope global 2>/dev/null | awk '/UP|UNKNOWN/ {print $3}' | while read -r cidr; do
		ip="${cidr%%/*}"
		case "$ip" in
			127.*|172.1[6-9].*|172.2[0-9].*|172.3[0-1].*) continue ;;
		esac
		echo "  http://${ip}:${PORT}/"
	done
	echo "  http://127.0.0.1:${PORT}/"
}

if (( PREPARE_ONLY )); then
	print_urls
	exit 0
fi

# Already listening (manual nohup or systemd)?
if ss -ltn 2>/dev/null | grep -qE ":${PORT}\\b"; then
	echo "already listening on :$PORT"
	print_urls
	exit 0
fi

if [[ -f "$PIDFILE" ]] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
	echo "already serving pid=$(cat "$PIDFILE") port=$PORT"
	print_urls
	exit 0
fi

nohup python3 -m http.server "$PORT" --directory "$DIR" >>"$LOG" 2>&1 &
echo $! >"$PIDFILE"
sleep 0.3
if ! kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
	echo "FAIL: http.server did not start — see $LOG" >&2
	exit 1
fi
print_urls
echo "pid=$(cat "$PIDFILE")"
