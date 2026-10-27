#!/usr/bin/env bash
# End-to-end Swiss hardware path: wait for SD → stage smoke → print GC steps →
# wait for remount with swiss-evidence.txt → run hw-complete ingest.
#
# Does not invent markers. Completes only when a real SD returns engine evidence.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Singleton: refuse a second oneshot (systemd unit + systemd-run transient).
mkdir -p .ai/logs
ONSHOT_LOCK=".ai/logs/swiss-hw-oneshot.lock"
exec 9>"$ONSHOT_LOCK"
if ! flock -n 9; then
	echo "FAIL: another gamecube-swiss-hw-oneshot.sh already holds $ONSHOT_LOCK" >&2
	echo "      Prefer: systemctl --user status xash3d-gc-swiss-hw-oneshot.service" >&2
	exit 0
fi

SKIP_NAMES="${SWISS_SD_WATCH_SKIP:-Extra Stream Expansion XASH3DGC XASHSMOKE}"
MAX_GIB="${SWISS_SD_WATCH_MAX_GIB:-512}"
TIMEOUT="${SD_WAIT_TIMEOUT:-7200}"
LOG=".ai/logs/swiss-hw-oneshot-$(date +%Y%m%d-%H%M%S).log"
mkdir -p "$(dirname "$LOG")"

exec > >(tee -a "$LOG") 2>&1

# Once-only operator notices during Phase-1 wait.
declare -A _GC_EXFAT_WARNED=()
declare -A _GC_RAW_DISK_NOTIFIED=()

echo "==> Swiss HW oneshot $(date -u +%Y-%m-%dT%H:%M:%SZ) timeout=${TIMEOUT}s"
echo "    log=$LOG"

# Kill any udisks monitor children if systemd restarts/stops us mid-wait.
UDISKS_PIDS=()
cleanup_udisks() {
	local p
	for p in "${UDISKS_PIDS[@]:-}"; do
		kill "$p" 2>/dev/null || true
	done
	UDISKS_PIDS=()
}
trap cleanup_udisks EXIT
# Main shell must ignore USR1: wait_for_candidate runs under $(...) so its trap
# lives in a subshell; udisks used to signal $$ (this PID) and kill the oneshot.
trap ':' USR1

if [[ ! -f OUT/bin/boot.dol ]]; then
	echo "FAIL: OUT/bin/boot.dol missing" >&2
	exit 1
fi
if [[ ! -d .ai/logs/swiss-smoke-stage/apps/xash3d-gc ]]; then
	echo "==> Building smoke stage"
	scripts/gamecube-swiss-smoke-image.sh --no-iso
fi

sha256sum OUT/bin/boot.dol OUT/xash3d-gc-swiss-smoke.img | sed 's/^/  /'

# Optional: operator set SWISS_FLASH_DEVICE=/dev/sdX SWISS_FLASH_YES=1 to dd
# before waiting for a mount (skips the "no reader" idle).
if [[ -n "${SWISS_FLASH_DEVICE:-}" ]]; then
	if [[ "${SWISS_FLASH_YES:-}" != "1" && "${SWISS_FLASH_YES:-}" != "yes" ]]; then
		echo "FAIL: SWISS_FLASH_DEVICE set but SWISS_FLASH_YES=1 required (destructive)" >&2
		exit 2
	fi
	echo "==> Pre-flash ${SWISS_FLASH_DEVICE} (SWISS_FLASH_DEVICE)"
	scripts/gamecube-swiss-sd-wait.sh --dd --device "$SWISS_FLASH_DEVICE" --yes
fi

is_skipped() {
	local name="$1" m src
	case " $SKIP_NAMES " in
		*" $name "*)
			# Smoke-image FAT labels: skip loop verify mounts only — a real
			# dd'd Swiss card remounts with the same label and must be accepted.
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

is_candidate() {
	local m="$1" fstype size_bytes max_bytes src disk tran rm hotplug
	[[ -d "$m" && -w "$m" ]] || return 1
	# Never treat EFI System Partitions as Swiss SD (Extra drive ESP).
	if [[ -d "$m/EFI" || -d "$m/efi" ]]; then
		return 1
	fi
	src="$(findmnt -n -o SOURCE --target "$m" 2>/dev/null || true)"
	case "$src" in
		/dev/loop*|loop\[*\]|*"["loop*) return 1 ;;
	esac
	if [[ -n "$src" && -b "$src" ]]; then
		disk="$(lsblk -no PKNAME "$src" 2>/dev/null || true)"
		[[ -z "$disk" ]] && disk="$(basename "$src" | sed 's/[0-9]*$//')"
		tran="$(lsblk -dn -o TRAN "/dev/$disk" 2>/dev/null || true)"
		rm="$(lsblk -dn -o RM "/dev/$disk" 2>/dev/null || true)"
		hotplug="$(lsblk -dn -o HOTPLUG "/dev/$disk" 2>/dev/null || true)"
		if [[ "$tran" != "usb" && "$rm" != "1" && "$hotplug" != "1" ]]; then
			return 1
		fi
	fi
	fstype="$(findmnt -no FSTYPE "$m" 2>/dev/null || true)"
	case "$fstype" in
		# Swiss/libdvm: FAT16/FAT32 only (exFAT will not mount on console).
		vfat|msdos|fat|fat32) ;;
		*) return 1 ;;
	esac
	size_bytes="$(findmnt -nb -o SIZE "$m" 2>/dev/null || echo 0)"
	max_bytes=$(( MAX_GIB * 1024 * 1024 * 1024 ))
	[[ -n "$size_bytes" && "$size_bytes" -gt 0 && "$size_bytes" -le "$max_bytes" ]] || return 1
	# Typical SD cards ≤128GiB — refuse huge shared-disk FAT volumes.
	(( size_bytes <= 128 * 1024 * 1024 * 1024 ))
}

find_candidate() {
	local name m
	for name in /run/media/tim/*; do
		[[ -d "$name" ]] || continue
		m="$name"
		name="$(basename "$m")"
		is_skipped "$name" && continue
		is_candidate "$m" || continue
		echo "$m"
		return 0
	done
	return 1
}

# Warn once per path when an exFAT card appears (libdvm will not mount it).
warn_exfat_once() {
	local name m fstype
	for name in /run/media/tim/*; do
		[[ -d "$name" ]] || continue
		m="$name"
		name="$(basename "$m")"
		is_skipped "$name" && continue
		fstype="$(findmnt -no FSTYPE "$m" 2>/dev/null || true)"
		[[ "$fstype" == "exfat" ]] || continue
		[[ -n "${_GC_EXFAT_WARNED[$m]:-}" ]] && continue
		_GC_EXFAT_WARNED[$m]=1
		echo "WARN: $m is exFAT — reformat FAT32 (Swiss/libdvm will not mount exFAT)" >&2
		command -v notify-send >/dev/null 2>&1 && \
			notify-send -u critical "Xash3D GC" "SD $name is exFAT — reformat FAT32 before Swiss" || true
	done
}

# Try mounting FAT partitions on USB/removable disks that did not auto-mount.
# Speeds Phase-1 when the card reader enumerates but desktop ignores the volume.
try_mount_raw_fat() {
	local part fstype disk tran rm hotplug
	while read -r part fstype; do
		[[ "$fstype" == "vfat" || "$fstype" == "msdos" ]] || continue
		# Skip already mounted.
		findmnt -n "$part" >/dev/null 2>&1 && continue
		disk="$(lsblk -no PKNAME "$part" 2>/dev/null || true)"
		[[ -n "$disk" ]] || continue
		case "$disk" in
			nvme*|sda|sdb|sdc) continue ;;
		esac
		tran="$(lsblk -dn -o TRAN "/dev/$disk" 2>/dev/null || true)"
		rm="$(lsblk -dn -o RM "/dev/$disk" 2>/dev/null || true)"
		hotplug="$(lsblk -dn -o HOTPLUG "/dev/$disk" 2>/dev/null || true)"
		if [[ "$tran" != "usb" && "$rm" != "1" && "$hotplug" != "1" ]]; then
			continue
		fi
		# Refuse EFI system partitions.
		if lsblk -no PARTTYPENAME "$part" 2>/dev/null | grep -qiE 'EFI|ESP'; then
			continue
		fi
		echo "==> Trying udisksctl mount $part ($fstype)" >&2
		udisksctl mount -b "$part" >/dev/null 2>&1 || true
	done < <(lsblk -nrpo NAME,FSTYPE)
}

# Notify once when a raw USB/removable disk appears (blank cards often do not auto-mount).
notify_raw_flash_once() {
	local name size tran rm hotplug type mib
	while read -r name size tran rm hotplug type; do
		[[ "$type" == "disk" ]] || continue
		if [[ "$tran" != "usb" && "$rm" != "1" && "$hotplug" != "1" ]]; then
			continue
		fi
		# Skip host / Expansion-class drives.
		case "$name" in
			nvme*|sda|sdb|sdc) continue ;;
		esac
		if (( size > MAX_GIB * 1024 * 1024 * 1024 )); then
			continue
		fi
		# Already have a usable FAT mount — no need to push dd.
		if find_candidate >/dev/null; then
			return 0
		fi
		[[ -n "${_GC_RAW_DISK_NOTIFIED[$name]:-}" ]] && continue
		_GC_RAW_DISK_NOTIFIED[$name]=1
		mib=$(( size / 1024 / 1024 ))
		echo "NOTE: raw flash candidate /dev/$name (${mib}MiB) — no FAT mount yet" >&2
		echo "      FAT32 + remount for rsync, or:" >&2
		echo "      scripts/gamecube-swiss-sd-wait.sh --dd --device /dev/$name --yes" >&2
		command -v notify-send >/dev/null 2>&1 && \
			notify-send "Xash3D GC" "USB disk /dev/$name (${mib}MiB) — format FAT32 or dd smoke.img" || true
	done < <(lsblk -nb -o NAME,SIZE,TRAN,RM,HOTPLUG,TYPE)
}

wait_for_candidate() {
	local deadline now m wake=0 udisks_pid="" self=$BASHPID
	deadline=$(( $(date +%s) + TIMEOUT ))
	echo "==> Waiting for Swiss SD FAT mount (≤${MAX_GIB}GiB, skip=[$SKIP_NAMES])…" >&2
	on_usr1() { wake=1; }
	trap on_usr1 USR1
	(
		# Must signal $self (this wait's BASHPID). $$ is the top-level script and
		# has no USR1 trap while wait_for_candidate runs under $(...).
		udisksctl monitor 2>&1 | while IFS= read -r line; do
			case "$line" in
				*Added*|*MountPoints*|*Filesystem*|*Job:*Completed*)
					kill -USR1 "$self" 2>/dev/null || true
					;;
			esac
		done
	) &
	udisks_pid=$!
	UDISKS_PIDS+=("$udisks_pid")
	echo "    udisks monitor pid=$udisks_pid (USR1 → $self)" >&2
	while true; do
		warn_exfat_once
		try_mount_raw_fat
		notify_raw_flash_once
		# Desktop drop-file flash (DEVICE=/dev/sdX + YES=1).
		if [[ -f "$HOME/Desktop/XASH3D-GC-FLASH-DEVICE.txt" ]]; then
			scripts/gamecube-swiss-flash-from-dropfile.sh >&2 || true
		fi
		# Off-host evidence drop (copy swiss-evidence.txt to Desktop).
		if [[ -f "$HOME/Desktop/XASH3D-GC-SWISS-EVIDENCE.txt" || -f "$HOME/Desktop/swiss-evidence.txt" ]]; then
			if scripts/gamecube-swiss-ingest-from-dropfile.sh >&2; then
				echo "==> Desktop evidence drop ingested during Phase-1 wait — exiting oneshot" >&2
				kill "$udisks_pid" 2>/dev/null || true
				UDISKS_PIDS=("${UDISKS_PIDS[@]/$udisks_pid}")
				trap - USR1
				# Signal parent to exit cleanly when running under $(...).
				printf '%s\n' "__EVIDENCE_DROP_DONE__"
				return 0
			fi
		fi
		if m="$(find_candidate)"; then
			kill "$udisks_pid" 2>/dev/null || true
			UDISKS_PIDS=("${UDISKS_PIDS[@]/$udisks_pid}")
			trap - USR1
			echo "READY: $m" >&2
			printf '%s\n' "$m"
			return 0
		fi
		now=$(date +%s)
		if (( now >= deadline )); then
			kill "$udisks_pid" 2>/dev/null || true
			UDISKS_PIDS=("${UDISKS_PIDS[@]/$udisks_pid}")
			trap - USR1
			echo "FAIL: timed out after ${TIMEOUT}s with no Swiss SD" >&2
			return 1
		fi
		wake=0
		# ~2s idle; USR1 from udisks breaks out early.
		# sleep exit >128 under set -e would kill the waiter (systemd: status=10/USR1).
		for _ in 1 2; do
			(( wake )) && break
			sleep 1 || true
		done
	done
}

# Safe eject after staging so the card can move to the GameCube.
unmount_for_console() {
	local m="$1" src
	src="$(findmnt -n -o SOURCE --target "$m" 2>/dev/null || true)"
	[[ -n "$src" ]] || return 0
	# Sync filesystem before eject.
	sync
	if udisksctl unmount -b "$src" 2>/dev/null; then
		echo "==> Unmounted $m ($src) — insert into GameCube / SD2SP2" >&2
		return 0
	fi
	echo "WARN: could not unmount $m ($src); unmount manually before console boot" >&2
	return 0
}

echo "==> Phase 1: stage smoke onto SD"
MOUNT="$(wait_for_candidate)" || {
	echo "FAIL: no Swiss SD candidate" >&2
	exit 1
}
if [[ "$MOUNT" == "__EVIDENCE_DROP_DONE__" ]]; then
	echo "==> Oneshot finished via Desktop evidence drop $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	cleanup_udisks
	trap - EXIT
	exit 0
fi
# If this remount already carries a complete Swiss run, skip restage and ingest.
phase1_skip_stage=0
EV0="$MOUNT/xash3d/valve/logs/swiss-evidence.txt"
if [[ -f "$EV0" ]] \
	&& grep -q "FAT preferred volume" "$EV0" \
	&& grep -qE "FAT volume ready (sd:|carda:|cardb:)/" "$EV0" \
	&& grep -q "FAT shutdown (return to Swiss loader via exit stub)" "$EV0" \
	&& grep -q "map loaded " "$EV0" \
	&& grep -qE "entity lump spawn ready|playable present .*nonblack=1" "$EV0"
then
	echo "==> Complete swiss-evidence already on $MOUNT — skip stage, ingest now"
	phase1_skip_stage=1
fi
if (( ! phase1_skip_stage )); then
	scripts/gamecube-swiss-sd-wait.sh --rsync --mount "$MOUNT"
	echo
	echo "== Next on GameCube =="
	echo "1. Insert card into SD2SP2 / SD Gecko"
	echo "2. Swiss → apps/xash3d-gc/boot.dol"
	echo "3. Expect splash → auto map c0a0e (gamecube.cfg + swissquit) → return to Swiss"
	echo "   (backup: hold Z+Start to quit)"
	echo "4. Reinsert card on this PC"
	echo
	unmount_for_console "$MOUNT"
	command -v notify-send >/dev/null 2>&1 && \
		notify-send "Xash3D GC" "SD staged+unmounted — boot apps/xash3d-gc/boot.dol on GameCube, then remount" || true
fi
echo "==> Phase 2: waiting for complete swiss-evidence.txt (FAT + playable + shutdown)…"

evidence_complete() {
	local f="$1"
	[[ -f "$f" ]] || return 1
	grep -q "FAT preferred volume" "$f" || return 1
	grep -qE "FAT volume ready (sd:|carda:|cardb:)/" "$f" || return 1
	grep -q "FAT shutdown (return to Swiss loader via exit stub)" "$f" || return 1
	grep -q "map loaded " "$f" || return 1
	grep -qE "entity lump spawn ready|playable present .*nonblack=1" "$f" || return 1
	return 0
}

# Scan every Swiss candidate mount (plus the Phase-1 mount) for evidence.
scan_evidence() {
	local m name cand
	# Prefer the staged mount first (may remount with same path).
	if [[ -n "${MOUNT:-}" && -d "$MOUNT" ]]; then
		for cand in \
			"$MOUNT/xash3d/valve/logs/swiss-evidence.txt" \
			"$MOUNT/valve/logs/swiss-evidence.txt" \
			"$MOUNT/swiss-evidence.txt"
		do
			if evidence_complete "$cand"; then
				EVIDENCE="$cand"
				return 0
			fi
		done
	fi
	for name in /run/media/tim/*; do
		[[ -d "$name" ]] || continue
		m="$name"
		name="$(basename "$m")"
		is_skipped "$name" && continue
		is_candidate "$m" || continue
		for cand in \
			"$m/xash3d/valve/logs/swiss-evidence.txt" \
			"$m/valve/logs/swiss-evidence.txt" \
			"$m/swiss-evidence.txt"
		do
			if evidence_complete "$cand"; then
				EVIDENCE="$cand"
				MOUNT="$m"
				return 0
			fi
		done
	done
	return 1
}

deadline=$(( $(date +%s) + TIMEOUT ))
EVIDENCE=""
phase2_wake=0
phase2_self=$BASHPID
on_phase2_usr1() { phase2_wake=1; }
trap on_phase2_usr1 USR1
(
	udisksctl monitor 2>&1 | while IFS= read -r line; do
		case "$line" in
			*Added*|*MountPoints*|*Filesystem*|*Job:*Completed*)
				kill -USR1 "$phase2_self" 2>/dev/null || true
				;;
		esac
	done
) &
phase2_udisks_pid=$!
UDISKS_PIDS+=("$phase2_udisks_pid")
echo "    phase2 udisks monitor pid=$phase2_udisks_pid (USR1 → $phase2_self)" >&2
while true; do
	if scan_evidence; then
		break
	fi
	if (( $(date +%s) >= deadline )); then
		kill "$phase2_udisks_pid" 2>/dev/null || true
		UDISKS_PIDS=("${UDISKS_PIDS[@]/$phase2_udisks_pid}")
		trap - USR1
		echo "FAIL: no complete swiss-evidence.txt within ${TIMEOUT}s" >&2
		echo "      Need: FAT preferred + volume ready + shutdown + map loaded + playable/entity" >&2
		echo "      After Swiss quit, remount the SD and re-run:" >&2
		echo "      scripts/gamecube-swiss-hw-complete.sh --log <mount>" >&2
		exit 2
	fi
	phase2_wake=0
	# sleep exit >128 under set -e would kill the waiter (systemd: status=10/USR1).
	for _ in 1 2 3; do
		(( phase2_wake )) && break
		sleep 1 || true
	done
	# Off-host evidence drop while waiting for remount.
	if [[ -f "$HOME/Desktop/XASH3D-GC-SWISS-EVIDENCE.txt" || -f "$HOME/Desktop/swiss-evidence.txt" ]]; then
		if scripts/gamecube-swiss-ingest-from-dropfile.sh >&2; then
			kill "$phase2_udisks_pid" 2>/dev/null || true
			UDISKS_PIDS=("${UDISKS_PIDS[@]/$phase2_udisks_pid}")
			trap - USR1
			cleanup_udisks
			trap - EXIT
			exit 0
		fi
	fi
done
kill "$phase2_udisks_pid" 2>/dev/null || true
UDISKS_PIDS=("${UDISKS_PIDS[@]/$phase2_udisks_pid}")
trap - USR1

echo "==> Found complete evidence: $EVIDENCE"
scripts/gamecube-swiss-hw-complete.sh --log "$MOUNT"
echo "==> Oneshot finished $(date -u +%Y-%m-%dT%H:%M:%SZ)"
# Successful finish: clear EXIT trap udisks list (already killed) and exit 0
# so systemd Restart=on-failure does not loop.
cleanup_udisks
trap - EXIT
