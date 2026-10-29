#!/usr/bin/env bash
# One-glance Swiss HW readiness (no Expansion deep scans).
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

echo "== Xash3D GC Swiss HW status $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
echo "mounts: $(ls /run/media/tim 2>/dev/null | tr '\n' ' ')"
oneshot=$(systemctl --user is-active xash3d-gc-swiss-hw-oneshot.service 2>/dev/null || true)
watch=$(systemctl --user is-active xash3d-gc-swiss-sd-watch.service 2>/dev/null || true)
wake=$(systemctl --user is-active xash3d-gc-swiss-sd-agent-wake.service 2>/dev/null || true)
quiet=$(systemctl --user is-active xash3d-gc-swiss-quiet-media-wait.service 2>/dev/null || true)
# Legacy transient name — should stay inactive once flock/Conflicts are in place.
live=$(systemctl --user is-active gc-swiss-oneshot-live.service 2>/dev/null || true)
echo "services(oneshot|sd-watch|agent-wake|quiet): ${oneshot:-?} ${watch:-?} ${wake:-?} ${quiet:-?}"
if [[ "${live:-}" == "active" ]]; then
	echo "WARN: gc-swiss-oneshot-live also active — stop it to avoid double-stage"
fi
if [[ -f OUT/bin/boot.dol ]]; then
	sha256sum OUT/bin/boot.dol OUT/xash3d-gc-swiss-smoke.img 2>/dev/null | sed 's/^/  /'
else
	echo "FAIL: OUT/bin/boot.dol missing"
fi
echo "stage cfg: $(tr '\n' ' ' < .ai/logs/swiss-smoke-stage/xash3d/valve/gamecube.cfg 2>/dev/null || echo missing)"
echo -n "candidates: "
found=0
for name in /run/media/tim/*; do
	[[ -d "$name" ]] || continue
	b="$(basename "$name")"
	# Mount labels can vary in case (EXTRA vs Extra); never report a configured
	# host drive as a Swiss staging candidate.
	case "${b,,}" in extra|stream|expansion) continue ;; esac
	# Smoke labels: skip loop verify mounts only (real dd'd cards remount same name).
	if [[ "$b" == "XASH3DGC" || "$b" == "XASHSMOKE" ]]; then
		src="$(findmnt -n -o SOURCE --target "$name" 2>/dev/null || true)"
		case "$src" in /dev/loop*|loop\[*\]|*"["loop*) continue ;; esac
	fi
	fstype="$(findmnt -no FSTYPE "$name" 2>/dev/null || true)"
	case "$fstype" in vfat|msdos|fat|fat32)
		# Skip EFI System Partitions (fixed-disk ESP, not Swiss SD).
		if [[ -d "$name/EFI" || -d "$name/efi" ]]; then
			continue
		fi
		src="$(findmnt -n -o SOURCE --target "$name" 2>/dev/null || true)"
		if [[ -n "$src" && -b "$src" ]]; then
			disk="$(lsblk -no PKNAME "$src" 2>/dev/null || true)"
			[[ -z "$disk" ]] && disk="$(basename "$src" | sed 's/[0-9]*$//')"
			tran="$(lsblk -dn -o TRAN "/dev/$disk" 2>/dev/null || true)"
			rm="$(lsblk -dn -o RM "/dev/$disk" 2>/dev/null || true)"
			hotplug="$(lsblk -dn -o HOTPLUG "/dev/$disk" 2>/dev/null || true)"
			if [[ "$tran" != "usb" && "$rm" != "1" && "$hotplug" != "1" ]]; then
				continue
			fi
		fi
		echo -n "$b "
		found=1
		for f in "$name/xash3d/valve/logs/swiss-evidence.txt" "$name/swiss-evidence.txt"; do
			[[ -f "$f" ]] && echo -n "[evidence:$f] "
		done
	;;
	exfat)
		echo -n "$b(exFAT-SKIP) "
	;; esac
done
(( found )) || echo -n "(none — insert Swiss SD)"
echo
if [[ -f "$HOME/Desktop/XASH3D-GC-FLASH-DEVICE.txt" ]]; then
	echo "flash-dropfile: present"
fi
if [[ -f "$HOME/Desktop/XASH3D-GC-SWISS-EVIDENCE.txt" || -f "$HOME/Desktop/swiss-evidence.txt" ]]; then
	echo "evidence-dropfile: present (Desktop)"
fi
[[ -f .ai/logs/hardware-goal-waiting.txt ]] && cat .ai/logs/hardware-goal-waiting.txt
