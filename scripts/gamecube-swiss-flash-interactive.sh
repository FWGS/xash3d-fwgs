#!/usr/bin/env bash
# Interactive Swiss SD flash: list candidates, confirm, dd smoke image.
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
IMG="${1:-OUT/xash3d-gc-swiss-smoke.img}"
[[ -f "$IMG" ]] || { echo "FAIL: missing $IMG — run scripts/gamecube-swiss-smoke-image.sh"; exit 1; }
echo "== Swiss interactive flash =="
sha256sum "$IMG" OUT/bin/boot.dol | sed 's/^/  /'
echo
echo "Candidate removable disks (≤512GiB USB/RM):"
mapfile -t lines < <(scripts/gamecube-swiss-list-flash-targets.sh 2>/dev/null | grep -E '^/dev/' || true)
if ((${#lines[@]}==0)); then
  echo "  (none) — plug FAT32 SD / USB reader, then re-run"
  echo "  Or: systemctl --user status xash3d-gc-swiss-hw-oneshot.service"
  exit 1
fi
printf '%s\n' "${lines[@]}"
echo
read -r -p "Device to OVERWRITE (e.g. /dev/sdd): " DEV
[[ -b "$DEV" ]] || { echo "FAIL: not a block device: $DEV"; exit 1; }
# Refuse if >512GiB or matches known fixed disks
SIZE=$(lsblk -bno SIZE "$DEV" 2>/dev/null | head -1)
MAX=$((512*1024*1024*1024))
(( SIZE > 0 && SIZE <= MAX )) || { echo "FAIL: size check"; exit 1; }
MODEL=$(lsblk -dn -o MODEL,VENDOR,TRAN,SIZE "$DEV")
echo "ABOUT TO DESTROY: $DEV ($MODEL)"
read -r -p "Type YES to dd: " OK
[[ "$OK" == "YES" ]] || { echo "aborted"; exit 1; }
sudo dd if="$IMG" of="$DEV" bs=4M status=progress conv=fsync
sync
echo "DONE. Eject, insert into SD2SP2/SD Gecko, boot Swiss → apps/xash3d-gc/boot.dol"
echo "Then remount SD here — oneshot will ingest swiss-evidence.txt"
