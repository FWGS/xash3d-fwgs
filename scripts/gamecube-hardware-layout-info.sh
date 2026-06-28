#!/usr/bin/env bash
# Print exact file placement instructions for real GameCube boot routes.
set -euo pipefail

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-hardware-layout-info.sh [--route all|sd|disc|memcard]

Prints hardware media layout instructions for the Xash3D GameCube port.
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
		sd|disc|memcard|all)
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
	sd|disc|memcard|all) ;;
	*)
		echo "unknown route: $route" >&2
		usage >&2
		exit 2
		;;
esac

print_sd() {
	cat <<'EOF'
== SD Gecko / SD2SP2 route ==
Copy:
  OUT/bin/boot.dol -> /boot.dol
  legal Half-Life assets -> /xash3d/valve/

Required smoke layout:
  /boot.dol
  /xash3d/valve/liblist.gam
  /xash3d/valve/gfx.wad
  /xash3d/valve/maps/c0a0e.bsp
  /xash3d/valve/models/
  /xash3d/valve/sprites/
  /xash3d/valve/sound/

Use Swiss or an equivalent homebrew loader to boot /boot.dol.
EOF
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

Use SD in parallel when validating saves or config writes.
EOF
}

print_memcard() {
	cat <<'EOF'
== Memory Card assisted route ==
Memory Cards are not a full asset route for Half-Life content.

Use Memory Card only for loader/bootstrap experiments:
  Memory Card: loader/bootstrap state
  SD or Disc: /xash3d/valve/ assets

Record Memory Card slot, card size, loader, and whether writable state is routed
to SD or intentionally unavailable.
EOF
}

case "$route" in
	all)
		print_sd
		echo
		print_disc
		echo
		print_memcard
		;;
	sd) print_sd ;;
	disc) print_disc ;;
	memcard) print_memcard ;;
esac
