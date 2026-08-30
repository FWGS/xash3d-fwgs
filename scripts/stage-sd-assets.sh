#!/bin/bash
# stage-sd-assets.sh - Stage Half-Life assets for Swiss/libdvm GameCube volumes
#
# Prepares the host-side SD/USB tree that Swiss mounts as:
#   sd:/     SD2SP2
#   carda:/  SD Gecko Slot A
#   cardb:/  SD Gecko Slot B
#
# Usage:
#   ./stage-sd-assets.sh <source_dir> <sd_card_path> [--route sd|carda|cardb|sdgecko]
#
# Example:
#   ./stage-sd-assets.sh /path/to/Half-Life /media/user/SDCARD --route sd
#   ./stage-sd-assets.sh /path/to/Half-Life /media/user/SDGECKO --route carda
#
# Layout on the mounted host filesystem (same relative paths for every route):
#   <mount>/apps/xash3d-gc/boot.dol
#   <mount>/xash3d/valve/
#   <mount>/xash3d/valve/save/
#   <mount>/xash3d/valve/logs/
#   <mount>/xash3d/valve/screenshots/

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

ROUTE="sd"
SOURCE_DIR=""
SD_CARD_PATH=""

print_usage() {
    echo "Usage: $0 <source_dir> <sd_card_path> [--route sd|carda|cardb|sdgecko]"
    echo ""
    echo "Arguments:"
    echo "  source_dir    Path to Half-Life assets (e.g., /path/to/Half-Life)"
    echo "  sd_card_path  Host mount point for the media Swiss will see"
    echo "  --route       Swiss/libdvm volume label for operator messaging"
    echo "                (default: sd). sdgecko is an alias for carda."
    echo ""
    echo "Swiss volume prefixes (libdvm):"
    echo "  sd:/     SD2SP2"
    echo "  carda:/  SD Gecko Slot A"
    echo "  cardb:/  SD Gecko Slot B"
    echo ""
    echo "Required media layout (host paths under the mount):"
    echo "  apps/xash3d-gc/boot.dol"
    echo "  xash3d/valve/"
    echo "  xash3d/valve/save/"
    echo "  xash3d/valve/logs/"
    echo "  xash3d/valve/screenshots/"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            print_usage
            exit 0
            ;;
        --route)
            ROUTE="${2:-}"
            shift 2
            ;;
        --route=*)
            ROUTE="${1#--route=}"
            shift
            ;;
        *)
            if [[ -z "$SOURCE_DIR" ]]; then
                SOURCE_DIR="$1"
            elif [[ -z "$SD_CARD_PATH" ]]; then
                SD_CARD_PATH="$1"
            else
                echo -e "${RED}Error: unexpected argument: $1${NC}" >&2
                print_usage >&2
                exit 1
            fi
            shift
            ;;
    esac
done

if [[ -z "$SOURCE_DIR" || -z "$SD_CARD_PATH" ]]; then
    print_usage
    exit 1
fi

case "$ROUTE" in
    sd|sd2sp2)
        ROUTE="sd"
        VOLUME_PREFIX="sd:/"
        ;;
    carda|sdgecko)
        ROUTE="carda"
        VOLUME_PREFIX="carda:/"
        ;;
    cardb)
        ROUTE="cardb"
        VOLUME_PREFIX="cardb:/"
        ;;
    *)
        echo -e "${RED}Error: unknown --route: $ROUTE (use sd|carda|cardb|sdgecko)${NC}" >&2
        exit 2
        ;;
esac

if [ ! -d "$SOURCE_DIR" ]; then
    echo -e "${RED}Error: Source directory does not exist: $SOURCE_DIR${NC}"
    exit 1
fi

if [ ! -d "$SD_CARD_PATH" ]; then
    echo -e "${RED}Error: SD card path does not exist: $SD_CARD_PATH${NC}"
    exit 1
fi

if [ ! -w "$SD_CARD_PATH" ]; then
    echo -e "${YELLOW}Warning: media may not be writable: $SD_CARD_PATH${NC}"
    if [[ -t 0 && -z "${STAGE_SD_NONINTERACTIVE:-}" ]]; then
        read -r -p "Continue anyway? (y/N) " -n 1
        echo
        if [[ ! ${REPLY:-} =~ ^[Yy]$ ]]; then
            exit 1
        fi
    else
        echo -e "${YELLOW}Non-interactive mode: continuing despite write warning${NC}"
    fi
fi

echo -e "${GREEN}Staging assets for Swiss volume ${VOLUME_PREFIX}...${NC}"
echo "Source: $SOURCE_DIR"
echo "Host mount: $SD_CARD_PATH"
echo "Route: $ROUTE ($VOLUME_PREFIX)"
echo ""

echo "Creating Swiss layout directories..."
mkdir -p "$SD_CARD_PATH/xash3d/valve/save"
mkdir -p "$SD_CARD_PATH/xash3d/valve/logs"
mkdir -p "$SD_CARD_PATH/xash3d/valve/screenshots"
mkdir -p "$SD_CARD_PATH/apps/xash3d-gc"

echo "Directory structure created."

echo ""
echo "Copying assets..."

ASSET_DIRS=("models" "sound" "materials" "maps" "scripts" "cfg" "resource" "fonts" "particles")

for dir in "${ASSET_DIRS[@]}"; do
    if [ -d "$SOURCE_DIR/$dir" ]; then
        echo "  Copying $dir..."
        cp -r "$SOURCE_DIR/$dir" "$SD_CARD_PATH/xash3d/valve/"
    else
        echo "  Skipping $dir (not found)"
    fi
done

echo "Copying specific files..."
for file in "gameinfo.txt" "halflife.gcd" "valve.gcd"; do
    if [ -f "$SOURCE_DIR/$file" ]; then
        echo "  Copying $file..."
        cp "$SOURCE_DIR/$file" "$SD_CARD_PATH/xash3d/valve/"
    fi
done

echo ""
echo "Creating PK3 archives..."

create_pk3() {
    local dir_name="$1"
    local source_path="$2"
    local output_path="$3"

    if [ ! -d "$source_path/$dir_name" ]; then
        echo "  Skipping $dir_name (not found)"
        return
    fi

    local pk3_name="${dir_name}.pk3"
    local temp_dir
    temp_dir=$(mktemp -d)

    cp -r "$source_path/$dir_name"/* "$temp_dir/"
    python3 "$(dirname "$0")/makepak.py" "$temp_dir" "$output_path/$pk3_name"
    rm -rf "$temp_dir"

    echo "  Created $pk3_name"
}

create_pk3 "models" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "sound" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "materials" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "maps" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "scripts" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "cfg" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"

echo ""
echo "Validating asset structure..."

VALIDATION_ERRORS=0

for dir in "models" "sound" "materials" "maps"; do
    if [ ! -d "$SD_CARD_PATH/xash3d/valve/$dir" ] && [ ! -f "$SD_CARD_PATH/xash3d/valve/${dir}.pk3" ]; then
        echo -e "${YELLOW}Warning: Required directory or PK3 not found: $dir${NC}"
        VALIDATION_ERRORS=$((VALIDATION_ERRORS + 1))
    fi
done

if [ ! -f "$SD_CARD_PATH/xash3d/valve/gameinfo.txt" ]; then
    echo -e "${YELLOW}Warning: gameinfo.txt not found${NC}"
    VALIDATION_ERRORS=$((VALIDATION_ERRORS + 1))
fi

echo ""
if [ "$VALIDATION_ERRORS" -eq 0 ]; then
    echo -e "${GREEN}Asset validation passed!${NC}"
else
    echo -e "${YELLOW}Asset validation completed with $VALIDATION_ERRORS warning(s)${NC}"
fi

echo ""
echo "=== Staging Summary ==="
echo "Source: $SOURCE_DIR"
echo "Host mount: $SD_CARD_PATH"
echo "Swiss route: $ROUTE ($VOLUME_PREFIX)"
echo "Assets copied to: $SD_CARD_PATH/xash3d/valve/"
echo ""
echo "Swiss media layout:"
echo "  ${VOLUME_PREFIX}apps/xash3d-gc/boot.dol"
echo "  ${VOLUME_PREFIX}xash3d/valve/"
echo "  ${VOLUME_PREFIX}xash3d/valve/save/"
echo "  ${VOLUME_PREFIX}xash3d/valve/logs/"
echo "  ${VOLUME_PREFIX}xash3d/valve/screenshots/"
echo ""
echo "To deploy:"
echo "  1. Copy OUT/bin/boot.dol to ${VOLUME_PREFIX}apps/xash3d-gc/boot.dol"
echo "  2. Insert media (SD2SP2 or SD Gecko) into the GameCube"
echo "  3. Boot the DOL through Swiss (libogc2)"
echo "  4. Confirm boot markers: FAT volume ready / FAT preferred volume"
echo ""
echo -e "${GREEN}Asset staging complete!${NC}"
