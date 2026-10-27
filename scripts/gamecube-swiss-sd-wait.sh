#!/usr/bin/env bash
# Wait for a removable FAT volume, then flash or rsync Swiss smoke media.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

IMG="${SWISS_SMOKE_IMG:-OUT/xash3d-gc-swiss-smoke.img}"
STAGE="${SWISS_SMOKE_STAGE:-.ai/logs/swiss-smoke-stage}"
MODE="rsync" # rsync|dd
DEVICE=""
MOUNT=""
TIMEOUT_SEC="${SD_WAIT_TIMEOUT:-600}"

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-swiss-sd-wait.sh [--dd|--rsync] [--device /dev/sdX] [--mount PATH]
                                         [--wait-device] [--yes]

Waits for a removable FAT mount (or uses --device/--mount), then deploys smoke media.

  --rsync        Copy .ai/logs/swiss-smoke-stage/ onto the mount (default, non-destructive)
  --dd           Raw-write OUT/xash3d-gc-swiss-smoke.img (DESTROYS the device)
  --device       Block device for --dd (e.g. /dev/sdd) — not a partition
  --mount        Already-mounted SD path for --rsync
  --wait-device  With --dd: wait for a new USB/removable disk (≤512GiB default), then flash
  --yes          Required with --wait-device to actually dd (otherwise only print device)

Environment:
  SD_WAIT_TIMEOUT   Seconds to wait for a new mount/device (default 600)
EOF
}

WAIT_DEVICE=0
YES=0

while (( $# > 0 )); do
	case "$1" in
		--dd) MODE=dd ;;
		--rsync) MODE=rsync ;;
		--device) DEVICE="$2"; shift ;;
		--mount) MOUNT="$2"; shift ;;
		--wait-device) WAIT_DEVICE=1 ;;
		--yes) YES=1 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

if [[ ! -f "$IMG" && "$MODE" == "dd" ]]; then
	echo "FAIL: missing $IMG — run scripts/gamecube-swiss-smoke-image.sh" >&2
	exit 1
fi
if [[ ! -d "$STAGE/apps/xash3d-gc" && "$MODE" == "rsync" ]]; then
	echo "FAIL: missing $STAGE — run scripts/gamecube-swiss-smoke-image.sh" >&2
	exit 1
fi

is_candidate_mount() {
	local m="$1"
	local src="" fstype="" disk="" tran="" rm="" hotplug=""
	[[ -n "$m" && -d "$m" && -w "$m" ]] || return 1
	case "$m" in
		/|/boot|/boot/*|/home|/home/*|/mnt/datasets|/mnt/datasets/*) return 1 ;;
		/run/media/tim/Extra|/run/media/tim/Stream|/run/media/tim/Expansion) return 1 ;;
		# Own smoke FAT image label (loop-mounted for host verify) — never flash onto itself.
		# Real dd'd cards remount with the same label; allow those (loop check below).
		/run/media/*/XASH3DGC|/media/*/XASH3DGC|/run/media/*/XASHSMOKE|/media/*/XASHSMOKE)
			src="$(findmnt -n -o SOURCE --target "$m" 2>/dev/null || true)"
			case "$src" in
				/dev/loop*|loop\[*\]|*"["loop*) return 1 ;;
				*) ;; # real disk OK
			esac
			;;
		/run/media/*|/media/*) ;;
		*) return 1 ;;
	esac
	# Refuse EFI System Partitions (Extra/sata ESP looked like a Swiss FAT card).
	if [[ -d "$m/EFI" || -d "$m/efi" ]]; then
		echo "FAIL: refusing EFI volume $m" >&2
		return 1
	fi
	# Refuse loop-backed mounts (e.g. udisksctl loop-setup of *.img).
	src="$(findmnt -n -o SOURCE --target "$m" 2>/dev/null || true)"
	case "$src" in
		/dev/loop*|loop\[*\]|*"["loop*) return 1 ;;
	esac
	# Require USB or removable — never stage onto fixed-disk FAT.
	if [[ -n "$src" && -b "$src" ]]; then
		disk="$(lsblk -no PKNAME "$src" 2>/dev/null || true)"
		[[ -z "$disk" ]] && disk="$(basename "$src" | sed 's/[0-9]*$//')"
		tran="$(lsblk -dn -o TRAN "/dev/$disk" 2>/dev/null || true)"
		rm="$(lsblk -dn -o RM "/dev/$disk" 2>/dev/null || true)"
		hotplug="$(lsblk -dn -o HOTPLUG "/dev/$disk" 2>/dev/null || true)"
		if [[ "$tran" != "usb" && "$rm" != "1" && "$hotplug" != "1" ]]; then
			echo "FAIL: refusing non-removable $m (tran=$tran rm=$rm)" >&2
			return 1
		fi
	fi
	fstype="$(findmnt -n -o FSTYPE --target "$m" 2>/dev/null || true)"
	case "$fstype" in
		# Swiss/libdvm mounts FAT16/FAT32 — refuse exFAT (common default on large cards).
		vfat|msdos|fat|fat32|"" ) return 0 ;;
		exfat)
			echo "FAIL: $m is exFAT — reformat FAT32 before Swiss staging (libdvm)" >&2
			return 1
			;;
		*) return 1 ;;
	esac
}

snapshot_mounts() {
	find /run/media /media -maxdepth 3 -type d 2>/dev/null | sort -u || true
}

wait_for_mount() {
	local before after m
	before="$(snapshot_mounts)"
	echo "Waiting up to ${TIMEOUT_SEC}s for a removable FAT mount…" >&2
	echo "Plug in SD2SP2 / SD Gecko (FAT32), then mount it in the file manager." >&2
	local deadline=$((SECONDS + TIMEOUT_SEC))
	local last_note=0
	while (( SECONDS < deadline )); do
		after="$(snapshot_mounts)"
		while IFS= read -r m; do
			[[ -z "$m" ]] && continue
			if ! grep -qxF "$m" <<<"$before"; then
				if is_candidate_mount "$m"; then
					printf '%s\n' "$m"
					return 0
				fi
			fi
		done <<<"$after"
		if (( SECONDS - last_note >= 30 )); then
			echo "…still waiting ($((deadline - SECONDS))s left)" >&2
			last_note=$SECONDS
		fi
		sleep 2
	done
	return 1
}

if [[ "$MODE" == "rsync" ]]; then
	if [[ -z "$MOUNT" ]]; then
		MOUNT="$(wait_for_mount)" || {
			echo "FAIL: no new removable mount within ${TIMEOUT_SEC}s" >&2
			exit 1
		}
	fi
	if ! is_candidate_mount "$MOUNT"; then
		echo "FAIL: refusing mount $MOUNT (not a removable media path)" >&2
		exit 1
	fi
	echo "==> rsync smoke stage → $MOUNT"
	# Never clobber hardware evidence written on-console (oneshot Phase-2).
	rsync -a --info=progress2 \
		--exclude 'xash3d/valve/logs/swiss-evidence.txt' \
		--exclude 'valve/logs/swiss-evidence.txt' \
		--exclude 'swiss-evidence.txt' \
		"$STAGE"/ "$MOUNT"/
	sync
	echo "DONE. Swiss: apps/xash3d-gc/boot.dol  or  xash3d-gc.iso"
	echo "After quit: scripts/gamecube-swiss-evidence.sh --log $MOUNT"
	exit 0
fi

# dd mode
MAX_GIB="${SWISS_SD_WATCH_MAX_GIB:-512}"
list_flash_candidates() {
	echo "Candidate flash devices (USB/removable disks ≤${MAX_GIB}GiB; verify before dd):" >&2
	lsblk -dn -o NAME,TRAN,RM,SIZE,MODEL,TYPE 2>/dev/null | while read -r name tran rm size model type; do
		[[ "$type" == "disk" ]] || continue
		dev="/dev/$name"
		case "$dev" in
			/dev/nvme*|/dev/sda|/dev/sdb|/dev/sdc) continue ;;
		esac
		if [[ "$rm" == "1" || "$tran" == "usb" ]]; then
			printf '  %s  tran=%s rm=%s size=%s model=%s\n' "$dev" "$tran" "$rm" "$size" "${model:-?}" >&2
		fi
	done
}

snapshot_disks() {
	lsblk -dn -o NAME,TYPE 2>/dev/null | awk '$2=="disk" {print "/dev/"$1}' | sort
}

is_safe_flash_disk() {
	local dev="$1" tran="" rm="" size_b=0 max_bytes
	[[ -b "$dev" ]] || return 1
	case "$dev" in
		/dev/nvme*|/dev/sda|/dev/sdb|/dev/sdc) return 1 ;;
		/dev/loop*) return 1 ;;
	esac
	tran="$(lsblk -dn -o TRAN "$dev" 2>/dev/null || true)"
	rm="$(lsblk -dn -o RM "$dev" 2>/dev/null || true)"
	size_b="$(lsblk -dn -b -o SIZE "$dev" 2>/dev/null || echo 0)"
	# Refuse multi-TB USB HDDs; allow common Swiss SD sizes (≤512GiB default).
	max_bytes=$(( MAX_GIB * 1024 * 1024 * 1024 ))
	if (( size_b > max_bytes )); then
		return 1
	fi
	# Require USB or removable bit.
	[[ "$rm" == "1" || "$tran" == "usb" ]] || return 1
	return 0
}

wait_for_flash_device() {
	local before after d
	before="$(snapshot_disks)"
	echo "Waiting up to ${TIMEOUT_SEC}s for a new USB/removable disk (≤${MAX_GIB}GiB)…" >&2
	echo "Plug the SD card reader now. Expansion/system disks are ignored." >&2
	local deadline=$((SECONDS + TIMEOUT_SEC))
	local last_note=0
	while (( SECONDS < deadline )); do
		after="$(snapshot_disks)"
		while IFS= read -r d; do
			[[ -z "$d" ]] && continue
			if ! grep -qxF "$d" <<<"$before"; then
				if is_safe_flash_disk "$d"; then
					printf '%s\n' "$d"
					return 0
				fi
				echo "…ignoring new disk $d (not a safe SD-sized USB/removable target)" >&2
			fi
		done <<<"$after"
		if (( SECONDS - last_note >= 30 )); then
			echo "…still waiting ($((deadline - SECONDS))s left)" >&2
			last_note=$SECONDS
		fi
		sleep 2
	done
	return 1
}

if [[ -z "$DEVICE" ]]; then
	if (( WAIT_DEVICE )); then
		DEVICE="$(wait_for_flash_device)" || {
			echo "FAIL: no new flashable USB/removable disk within ${TIMEOUT_SEC}s" >&2
			list_flash_candidates
			exit 1
		}
		echo "Detected flash target: $DEVICE" >&2
		command -v notify-send >/dev/null 2>&1 && \
			notify-send "Xash3D GC" "SD reader detected: $DEVICE" || true
		if (( ! YES )); then
			echo "Refusing to dd without --yes. Re-run:" >&2
			echo "  scripts/gamecube-swiss-sd-wait.sh --dd --device $DEVICE --yes" >&2
			exit 0
		fi
	else
		echo "FAIL: --dd requires --device /dev/sdX or --wait-device" >&2
		list_flash_candidates
		echo "Tip: scripts/gamecube-swiss-sd-wait.sh --dd --wait-device --yes" >&2
		echo "     Or --rsync after the card mounts under /run/media/tim/" >&2
		exit 2
	fi
fi
if [[ ! -b "$DEVICE" ]]; then
	echo "FAIL: $DEVICE is not a block device" >&2
	list_flash_candidates
	exit 1
fi
if ! is_safe_flash_disk "$DEVICE"; then
	echo "FAIL: refusing unsafe flash target $DEVICE" >&2
	exit 1
fi
# Refuse if device is mounted
if findmnt -n -S "$DEVICE" >/dev/null 2>&1 || findmnt -n -S "${DEVICE}1" >/dev/null 2>&1; then
	echo "FAIL: $DEVICE (or a partition) is currently mounted — unmount first" >&2
	exit 1
fi
echo "==> dd $IMG → $DEVICE (DESTROYS CONTENTS)"
sudo dd if="$IMG" of="$DEVICE" bs=4M status=progress conv=fsync
echo "DONE. Insert into SD2SP2/SD Gecko and boot via Swiss."
echo "After quit: scripts/gamecube-swiss-hw-complete.sh --log /path/to/sd"
