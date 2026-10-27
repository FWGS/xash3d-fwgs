#!/usr/bin/env bash
# List block devices that look like Swiss SD flash targets (≤128GiB removable/USB).
# Does not write anything. Use with gamecube-swiss-sd-wait.sh --dd --device …
set -euo pipefail

MAX_GIB="${SWISS_SD_WATCH_MAX_GIB:-512}"
max_bytes=$(( MAX_GIB * 1024 * 1024 * 1024 ))

echo "== Swiss SD flash candidates (≤${MAX_GIB}GiB, USB/removable disks) =="
echo "WARNING: dd DESTROYS the whole disk. Confirm the device before flashing."
echo

found=0
while read -r name size tran rm hotplug type; do
	[[ "$type" == "disk" ]] || continue
	# Prefer USB or kernel-removable/hotplug disks.
	if [[ "$tran" != "usb" && "$rm" != "1" && "$hotplug" != "1" ]]; then
		continue
	fi
	# Skip huge Expansion-class drives.
	if (( size > max_bytes )); then
		continue
	fi
	# Skip if it looks like the host boot disk (has / or /boot/efi mounted under it).
	if findmnt -n -o TARGET -S "/dev/$name"* 2>/dev/null | grep -qxE '/|/boot|/boot/efi'; then
		continue
	fi
	label="$(lsblk -ndo LABEL "/dev/$name" 2>/dev/null | tr '\n' ' ' | sed 's/ *$//')"
	fstype="$(lsblk -ndo FSTYPE "/dev/$name" 2>/dev/null | tr '\n' ' ' | sed 's/ *$//')"
	# Skip disks whose only interesting label is an EFI System Partition.
	case " $label " in
		*" EFI "*|*" ESP "*) continue ;;
	esac
	if lsblk -nro PARTTYPENAME "/dev/$name" 2>/dev/null | grep -qx 'EFI System'; then
		other="$(lsblk -nro PARTTYPENAME "/dev/$name" 2>/dev/null | grep -cvx 'EFI System' || true)"
		if [[ "${other:-0}" -eq 0 ]]; then
			continue
		fi
	fi
	mounts="$(findmnt -n -o TARGET -S "/dev/$name"* 2>/dev/null | tr '\n' ' ' || true)"
	mib=$(( size / 1024 / 1024 ))
	echo "/dev/$name  ${mib}MiB  tran=$tran rm=$rm  label=[$label] fstype=[$fstype]"
	echo "  mounts: ${mounts:-"(none)"}"
	echo "  flash:  sudo dd if=OUT/xash3d-gc-swiss-smoke.img of=/dev/$name bs=4M status=progress conv=fsync"
	echo "  or:     scripts/gamecube-swiss-sd-wait.sh --dd --device /dev/$name --yes"
	echo
	found=1
done < <(lsblk -nb -o NAME,SIZE,TRAN,RM,HOTPLUG,TYPE)

if (( ! found )); then
	echo "(none found — plug a USB SD reader, then re-run)"
	echo "Also: scripts/gamecube-swiss-hw-flash-when-ready.sh   # waits for mount + rsync"
	exit 1
fi
