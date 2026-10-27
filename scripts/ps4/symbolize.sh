#!/bin/sh
# Resolves "module+0xoffset" lines from crash and watchdog dumps in stdout.txt
# Usage: scripts/ps4/symbolize.sh [stdout.txt|PS4 IP address] [extra directories with game .so files]
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build"
INPUT=${1:-stdout.txt}
shift || true

ADDR2LINE=$(command -v llvm-addr2line-18 || command -v llvm-addr2line || command -v addr2line)

if [ ! -f "$INPUT" ]; then
	# treat as console address
	curl -s "ftp://$INPUT:2121/data/xash/stdout.txt" > /tmp/xash-ps4-stdout.txt
	INPUT=/tmp/xash-ps4-stdout.txt
fi

find_elf()
{
	case "$1" in
	eboot.bin) echo "$BUILD/engine/xash" ;;
	filesystem_stdio.prx) echo "$BUILD/filesystem/filesystem_stdio.so" ;;
	libmenu.prx) echo "$BUILD/3rdparty/mainui/libmenu.so" ;;
	libref_*.prx) ls "$BUILD"/ref/*/"$(basename "$1" .prx).so" 2>/dev/null | head -n 1 ;;
	*)
		# game libraries: look in extra directories by name without extension
		name=$(basename "$1" .prx)
		for dir in $EXTRA_DIRS; do
			f=$(find "$dir" -name "$name.so" 2>/dev/null | head -n 1)
			[ -n "$f" ] && echo "$f" && return
		done
		;;
	esac
}

EXTRA_DIRS="$*"

grep -oE "[A-Za-z0-9_.-]+\.(bin|prx|sprx)\+0x[0-9a-f]+" "$INPUT" | awk "!seen[\$0]++" | while read -r entry; do
	module=${entry%%+*}
	offset=${entry#*+}
	elf=$(find_elf "$module")
	if [ -n "$elf" ] && [ -f "$elf" ]; then
		printf '%-40s %s\n' "$entry" "$("$ADDR2LINE" -f -C -i -e "$elf" "$offset" < /dev/null | paste -sd ' ' -)"
	else
		printf '%-40s (system module)\n' "$entry"
	fi
done
