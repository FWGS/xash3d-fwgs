#!/bin/bash
# stage-sd-assets.sh - Stage Half-Life onto a Swiss FAT32 card for real GameCube hardware
#
# Usage:
#   ./stage-sd-assets.sh <Half-Life dir> <sd_card_mount> [--route sd|carda|cardb|sdgecko] [options]
#
# Thin wrapper over scripts/stage_sd_assets.py, which writes the same payload
# as the retail Dolphin disc (see that script's --help for options):
#   <mount>/apps/xash3d-gc/boot.dol
#   <mount>/xash3d/valve/            (+ save/ logs/ screenshots/)
#
# Swiss volume prefixes (libdvm): sd:/ SD2SP2, carda:/ SD Gecko A, cardb:/ SD Gecko B.

set -euo pipefail

exec python3 "$(dirname "$0")/stage_sd_assets.py" "$@"
