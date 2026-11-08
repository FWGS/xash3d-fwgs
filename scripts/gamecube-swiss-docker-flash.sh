#!/usr/bin/env bash
# Flash Swiss smoke img to a USB SD reader without host sudo (needs docker group).
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
IMG="${1:-OUT/xash3d-gc-swiss-smoke.img}"
DEV="${2:-/dev/sda}"
[[ -f "$IMG" ]] || { echo "FAIL: missing $IMG"; exit 1; }
[[ -b "$DEV" ]] || { echo "FAIL: not a block device: $DEV"; exit 1; }
# Unmount any partitions on DEV
for p in "${DEV}"*; do
	[[ -b "$p" && "$p" != "$DEV" ]] || continue
	udisksctl unmount -b "$p" 2>/dev/null || true
done
udisksctl unmount -b "$DEV" 2>/dev/null || true
sha256sum "$IMG" | sed 's/^/  /'
echo "==> docker dd $IMG → $DEV"
docker run --rm --privileged -v "$(realpath "$IMG"):/smoke.img:ro" alpine:3.20 \
	sh -c "dd if=/smoke.img of=$DEV bs=4M conv=fsync && sync"
sync
sleep 1
docker run --rm --privileged alpine:3.20 sh -c "blockdev --rereadpt $DEV 2>/dev/null || true" || true
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT "$DEV"
echo "DONE. Mount, verify apps/xash3d-gc/boot.dol, then eject for GameCube."
