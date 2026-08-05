#!/usr/bin/env bash
# Print exact file placement instructions for real GameCube boot routes.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-hardware-layout-info.sh [--route all|sd|sd2sp2|sdgecko|carda|cardb|disc|memcard]

Prints hardware media layout instructions for the Xash3D GameCube port.
Swiss/libdvm volumes: sd: (SD2SP2), carda:/cardb: (SD Gecko).
EOF
}

route="all"
while [[ $# -gt 0 ]]; do
	case "$1" in
		--route)
			route="${2:-}"
			shift 2
			;;
		--route=*)
			route="${1#--route=}"
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		sd|sd2sp2|sdgecko|carda|cardb|disc|memcard|all)
			route="$1"
			shift
			;;
		*)
			echo "unknown argument: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

case "$route" in
	sd|sd2sp2|sdgecko|carda|cardb|disc|memcard|all) ;;
	*)
		echo "unknown route: $route" >&2
		usage >&2
		exit 2
		;;
esac

print_volume() {
	local vol="$1"
	python3 "$ROOT/scripts/waifulib/gamecube_storage.py" --layout "$vol"
	echo
	echo "Use Swiss (libogc2 DOL) to boot the DOL from this volume."
}

print_sd() {
	print_volume "sd:/"
}

print_carda() {
	print_volume "carda:/"
}

print_cardb() {
	print_volume "cardb:/"
}

print_sdgecko() {
	echo "== SD Gecko routes (libdvm carda:/cardb:) =="
	echo
	print_carda
	echo
	print_cardb
}

print_disc() {
	cat <<'EOF'
== Disc image route ==
Build:
  scripts/build-gamecube-disc.py --smoke-map c0a0e --output OUT/xash3d-gc.iso

Expected read-only image content:
  /boot.dol
  /xash3d/valve/
  /xash3d/valve/extras.pk3

Use SD2SP2 (sd:) or SD Gecko (carda:/cardb:) in parallel when validating saves
or config writes.
EOF
}

print_memcard() {
	cat <<'EOF'
== Memory Card assisted route ==
Memory Cards are not a full asset route for Half-Life content.

Use Memory Card only for loader/bootstrap experiments:
  Memory Card: loader/bootstrap state
  SD2SP2 / SD Gecko or Disc: /xash3d/valve/ assets

Record Memory Card slot, card size, loader, and whether writable state is routed
to FAT media or intentionally unavailable.
EOF
}

case "$route" in
	all)
		print_sd
		echo
		print_sdgecko
		echo
		print_disc
		echo
		print_memcard
		;;
	sd|sd2sp2) print_sd ;;
	sdgecko) print_sdgecko ;;
	carda) print_carda ;;
	cardb) print_cardb ;;
	disc) print_disc ;;
	memcard) print_memcard ;;
esac
