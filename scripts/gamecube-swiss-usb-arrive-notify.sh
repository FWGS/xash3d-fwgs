#!/usr/bin/env bash
# Loud notify when a new USB block disk appears (SD reader re-plug).
# Complements the oneshot FAT waiter — fires even before automount.
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"
LOG=".ai/logs/swiss-usb-arrive.log"
STATE=".ai/logs/swiss-usb-arrive.state"
mkdir -p .ai/logs
touch "$STATE"
exec >>"$LOG" 2>&1

seen_list() { cut -d' ' -f1 "$STATE" 2>/dev/null || true; }

while true; do
	now_ids=()
	while read -r name size tran rm hotplug type; do
		[[ "$type" == "disk" ]] || continue
		if [[ "$tran" != "usb" && "$rm" != "1" && "$hotplug" != "1" ]]; then
			continue
		fi
		case "$name" in nvme*) continue ;; esac
		# Skip huge host Expansion-class disks (>512GiB).
		if (( size > 512 * 1024 * 1024 * 1024 )); then
			continue
		fi
		now_ids+=("$name")
		if ! grep -qx "$name" "$STATE" 2>/dev/null; then
			mib=$(( size / 1024 / 1024 ))
			echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) NEW_USB /dev/$name ${mib}MiB tran=$tran"
			printf '%s\n' "$name" >>"$STATE"
			msg="USB disk /dev/$name (${mib}MiB) — oneshot will stage if FAT XASHSMOKE mounts"
			command -v notify-send >/dev/null 2>&1 && \
				notify-send -u critical "Xash3D GC SD reader?" "$msg" || true
			printf '%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ) $msg" \
				> "$HOME/Desktop/XASH3D-GC-USB-ARRIVE.txt" || true
		fi
	done < <(lsblk -nb -o NAME,SIZE,TRAN,RM,HOTPLUG,TYPE)
	# Drop disappeared disks from state so re-plug notifies again.
	tmp="$(mktemp)"
	for id in "${now_ids[@]:-}"; do
		echo "$id"
	done | sort -u >"$tmp"
	mv "$tmp" "$STATE"
	sleep 2
done
