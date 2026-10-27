#!/usr/bin/env bash
# Poll for Swiss SD media and rsync the smoke stage onto it.
# - Detects newly mounted volumes under /run/media/tim
# - Also restages any small FAT/exFAT mount missing/outdated boot.dol
# Skips Extra/Stream/Expansion and loop-mounted XASH3DGC smoke images.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

SKIP_NAMES="${SWISS_SD_WATCH_SKIP:-Extra Stream Expansion XASH3DGC XASHSMOKE}"
LOG="${SWISS_SD_WATCH_LOG:-.ai/logs/udev-sd-watch-live.log}"
MAX_GIB="${SWISS_SD_WATCH_MAX_GIB:-512}"
DOL_SRC="OUT/bin/boot.dol"
mkdir -p "$(dirname "$LOG")"

exec >>"$LOG" 2>&1
echo "udev-sd-watch: started $(date -u +%Y-%m-%dT%H:%M:%SZ) skip=[$SKIP_NAMES] max_gib=$MAX_GIB"

before="$(ls /run/media/tim 2>/dev/null | sort || true)"
staged_ok=0
waiting_notify=0

is_skipped() {
	local name="$1" m src
	case " $SKIP_NAMES " in
		*" $name "*)
			# Smoke-image FAT labels: skip loop verify mounts only, not a real
			# dd'd card that remounts as XASH3DGC/XASHSMOKE.
			if [[ "$name" == "XASH3DGC" || "$name" == "XASHSMOKE" ]]; then
				m="/run/media/tim/$name"
				src="$(findmnt -n -o SOURCE --target "$m" 2>/dev/null || true)"
				case "$src" in
					/dev/loop*|loop\[*\]|*"["loop*) return 0 ;;
					*) return 1 ;;
				esac
			fi
			return 0
			;;
	esac
	return 1
}

mount_is_swiss_candidate() {
	local m="$1"
	local fstype size_bytes max_bytes src tran rm hotplug disk size_gib

	[[ -d "$m" && -w "$m" ]] || return 1
	# Never stage onto EFI System Partitions (e.g. Extra drive's sda2).
	if [[ -d "$m/EFI" || -d "$m/efi" ]]; then
		echo "udev-sd-watch: refusing EFI volume $m" >&2
		return 1
	fi
	src="$(findmnt -n -o SOURCE --target "$m" 2>/dev/null || true)"
	case "$src" in
		/dev/loop*|loop\[*\]|*"["loop*) return 1 ;;
	esac
	# Require USB or kernel-removable media — refuse fixed SATA/NVMe FAT (ESP).
	if [[ -n "$src" && -b "$src" ]]; then
		disk="$(lsblk -no PKNAME "$src" 2>/dev/null || true)"
		[[ -z "$disk" ]] && disk="$(basename "$src" | sed 's/[0-9]*$//')"
		tran="$(lsblk -dn -o TRAN "/dev/$disk" 2>/dev/null || true)"
		rm="$(lsblk -dn -o RM "/dev/$disk" 2>/dev/null || true)"
		hotplug="$(lsblk -dn -o HOTPLUG "/dev/$disk" 2>/dev/null || true)"
		if [[ "$tran" != "usb" && "$rm" != "1" && "$hotplug" != "1" ]]; then
			echo "udev-sd-watch: refusing non-removable $m (tran=$tran rm=$rm)" >&2
			return 1
		fi
	fi
	fstype="$(findmnt -no FSTYPE "$m" 2>/dev/null || true)"
	case "$fstype" in
		# libdvm/Swiss mounts FAT16/FAT32 only — exFAT will black-hang on GC.
		vfat|msdos|fat|fat32) ;;
		exfat)
			echo "udev-sd-watch: refusing exFAT $m (reformat FAT32 for Swiss/libdvm)" >&2
			return 1
			;;
		*) return 1 ;;
	esac
	size_bytes="$(findmnt -nb -o SIZE "$m" 2>/dev/null || echo 0)"
	max_bytes=$(( MAX_GIB * 1024 * 1024 * 1024 ))
	if [[ -z "$size_bytes" || "$size_bytes" -le 0 || "$size_bytes" -gt "$max_bytes" ]]; then
		return 1
	fi
	# Swiss SD cards are typically ≤128GiB; refuse huge FAT volumes on shared disks.
	size_gib=$(( size_bytes / 1024 / 1024 / 1024 ))
	if (( size_gib > 128 )); then
		echo "udev-sd-watch: refusing oversized FAT $m (${size_gib}GiB > 128)" >&2
		return 1
	fi
	return 0
}

needs_stage() {
	local m="$1"
	local dst="$m/apps/xash3d-gc/boot.dol"

	[[ -f "$DOL_SRC" ]] || return 1
	# Prefer ingest over restage when complete Swiss evidence is already on-card.
	if evidence_present "$m"; then
		return 1
	fi
	if [[ ! -f "$dst" ]]; then
		return 0
	fi
	# Restage when packet DOL is newer/different than card.
	if ! cmp -s "$DOL_SRC" "$dst" 2>/dev/null; then
		return 0
	fi
	# Also require valve assets so we do not leave a DOL-only card.
	if [[ ! -f "$m/xash3d/valve/gfx.wad" ]]; then
		return 0
	fi
	return 1
}

evidence_present() {
	local m="$1" f
	for f in \
		"$m/xash3d/valve/logs/swiss-evidence.txt" \
		"$m/valve/logs/swiss-evidence.txt" \
		"$m/swiss-evidence.txt"
	do
		[[ -f "$f" ]] || continue
		grep -q "FAT preferred volume" "$f" 2>/dev/null || continue
		grep -q "FAT shutdown" "$f" 2>/dev/null || continue
		return 0
	done
	return 1
}

try_stage() {
	local m="$1"
	local why="$2"

	if evidence_present "$m"; then
		echo "udev-sd-watch: evidence present on $m — skip stage (ingest)"
		command -v notify-send >/dev/null 2>&1 && \
			notify-send "Xash3D GC" "Swiss evidence on $m — run hw-complete / oneshot Phase-2" || true
		return 0
	fi

	echo "udev-sd-watch: $why $m"
	if scripts/gamecube-swiss-sd-wait.sh --rsync --mount "$m"; then
		echo "udev-sd-watch: staged OK $(date -u +%H:%M:%S) mount=$m"
		command -v notify-send >/dev/null 2>&1 && \
			notify-send "Xash3D GC" "Swiss smoke staged on $m — boot on GameCube, then remount for ingest" || true
		staged_ok=1
		return 0
	fi
	echo "udev-sd-watch: stage failed for $m"
	return 1
}

scan_and_stage() {
	local after name m
	after="$(ls /run/media/tim 2>/dev/null | sort || true)"
	for name in $after; do
		is_skipped "$name" && continue
		m="/run/media/tim/$name"
		mount_is_swiss_candidate "$m" || continue

		# New mount since last snapshot.
		if ! grep -qxF "$name" <<<"$before"; then
			try_stage "$m" "detected new mount" || true
			continue
		fi
		# Already-mounted card missing/outdated packet (e.g. plugged before watch).
		if needs_stage "$m"; then
			try_stage "$m" "restage candidate" || true
		fi
	done
	before="$after"
}

# udisks events interrupt the poll sleep so a newly plugged SD is staged ASAP.
scan_now=0
on_exit() { kill "$udisks_pid" 2>/dev/null || true; }
on_usr1() { scan_now=1; }
trap on_exit EXIT
trap on_usr1 USR1
(
	udisksctl monitor 2>&1 | while IFS= read -r line; do
		case "$line" in
			*Added*|*MountPoints*|*Filesystem*|*Job:*Completed*)
				kill -USR1 $$ 2>/dev/null || true
				;;
		esac
	done
) &
udisks_pid=$!
echo "udev-sd-watch: udisks monitor pid=$udisks_pid (USR1 wake)"

while true; do
	scan_and_stage
	scan_now=0
	# Periodic reminder while no Swiss SD is present.
	now=$(date +%s)
	if (( now - ${waiting_notify:-0} >= 1800 )); then
		waiting_notify=$now
		echo "udev-sd-watch: still waiting for Swiss SD $(date -u +%Y-%m-%dT%H:%M:%SZ)"
		command -v notify-send >/dev/null 2>&1 && \
			notify-send "Xash3D GC" "Still waiting for Swiss SD (FAT32 ≤${MAX_GIB}GiB) — plug reader to auto-stage" || true
	fi
	# Up to ~2s idle; USR1 from udisks breaks out early via scan_now.
	for _ in 1 2; do
		(( scan_now )) && break
		sleep 1
	done
done
