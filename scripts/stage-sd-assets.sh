#!/bin/bash
# stage-sd-assets.sh - Stage Half-Life assets to SD card for GameCube
#
# This script helps prepare assets for deployment to a GameCube SD card.
# It creates PK3 archives and validates asset structure.
#
# Usage:
#   ./stage-sd-assets.sh <source_dir> <sd_card_path>
#
# Example:
#   ./stage-sd-assets.sh /home/tim/Desktop/xash3d-gc/Half-Life /media/user/SDCARD
#
# The script will:
# 1. Copy assets from source to SD card
# 2. Create PK3 archives for efficient loading
# 3. Validate asset structure and report issues

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_usage() {
    echo "Usage: $0 <source_dir> <sd_card_path>"
    echo ""
    echo "Arguments:"
    echo "  source_dir    Path to Half-Life assets (e.g., /path/to/Half-Life)"
    echo "  sd_card_path  Path to SD card mount point (e.g., /media/user/SDCARD)"
    echo ""
    echo "Example:"
    echo "  $0 /home/tim/Desktop/xash3d-gc/Half-Life /media/user/SDCARD"
    echo ""
    echo "The script will:"
    echo "  1. Copy assets to sd:/xash3d/valve/"
    echo "  2. Create PK3 archives for efficient loading"
    echo "  3. Validate asset structure"
    echo ""
    echo "Required SD card layout:"
    echo "  sd:/apps/xash3d-gc/boot.dol"
    echo "  sd:/xash3d/valve/           (Half-Life assets)"
    echo "  sd:/xash3d/valve/save/      (for save games)"
    echo "  sd:/xash3d/valve/logs/      (for logs)"
    echo "  sd:/xash3d/valve/screenshots/ (for screenshots)"
}

# Check arguments
if [ $# -lt 2 ]; then
    print_usage
    exit 1
fi

SOURCE_DIR="$1"
SD_CARD_PATH="$2"

# Validate source directory
if [ ! -d "$SOURCE_DIR" ]; then
    echo -e "${RED}Error: Source directory does not exist: $SOURCE_DIR${NC}"
    exit 1
fi

# Validate SD card path
if [ ! -d "$SD_CARD_PATH" ]; then
    echo -e "${RED}Error: SD card path does not exist: $SD_CARD_PATH${NC}"
    exit 1
fi

# Check if SD card is writable
if [ ! -w "$SD_CARD_PATH" ]; then
    echo -e "${YELLOW}Warning: SD card may not be writable: $SD_CARD_PATH${NC}"
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

echo -e "${GREEN}Staging assets to SD card...${NC}"
echo "Source: $SOURCE_DIR"
echo "Target: $SD_CARD_PATH"
echo ""

# Create SD card directory structure
echo "Creating SD card directory structure..."
mkdir -p "$SD_CARD_PATH/xash3d/valve/save"
mkdir -p "$SD_CARD_PATH/xash3d/valve/logs"
mkdir -p "$SD_CARD_PATH/xash3d/valve/screenshots"
mkdir -p "$SD_CARD_PATH/apps/xash3d-gc"

echo "Directory structure created."

# Copy assets to SD card
echo ""
echo "Copying assets to SD card..."

# List of directories to copy
ASSET_DIRS=("models" "sound" "materials" "maps" "scripts" "cfg" "resource" "fonts" "particles")

for dir in "${ASSET_DIRS[@]}"; do
    if [ -d "$SOURCE_DIR/$dir" ]; then
        echo "  Copying $dir..."
        cp -r "$SOURCE_DIR/$dir" "$SD_CARD_PATH/xash3d/valve/"
    else
        echo "  Skipping $dir (not found)"
    fi
done

# Copy specific files
echo "Copying specific files..."
for file in "gameinfo.txt" "halflife.gcd" "valve.gcd"; do
    if [ -f "$SOURCE_DIR/$file" ]; then
        echo "  Copying $file..."
        cp "$SOURCE_DIR/$file" "$SD_CARD_PATH/xash3d/valve/"
    fi
done

# Create PK3 archives for efficient loading
echo ""
echo "Creating PK3 archives..."

# Function to create PK3 archive
create_pk3() {
    local dir_name="$1"
    local source_path="$2"
    local output_path="$3"
    
    if [ ! -d "$source_path/$dir_name" ]; then
        echo "  Skipping $dir_name (not found)"
        return
    fi
    
    local pk3_name="${dir_name}.pk3"
    local temp_dir=$(mktemp -d)
    
    # Copy files to temp directory
    cp -r "$source_path/$dir_name"/* "$temp_dir/"
    
    # Create PK3 using makepak.py
    python3 "$(dirname "$0")/makepak.py" "$temp_dir" "$output_path/$pk3_name"
    
    # Clean up temp directory
    rm -rf "$temp_dir"
    
    echo "  Created $pk3_name"
}

# Create PK3 archives for common asset types
create_pk3 "models" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "sound" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "materials" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "maps" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "scripts" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"
create_pk3 "cfg" "$SD_CARD_PATH/xash3d/valve" "$SD_CARD_PATH/xash3d/valve"

# Validate asset structure
echo ""
echo "Validating asset structure..."

VALIDATION_ERRORS=0

# Check for required directories
for dir in "models" "sound" "materials" "maps"; do
    if [ ! -d "$SD_CARD_PATH/xash3d/valve/$dir" ] && [ ! -f "$SD_CARD_PATH/xash3d/valve/${dir}.pk3" ]; then
        echo -e "${YELLOW}Warning: Required directory or PK3 not found: $dir${NC}"
        VALIDATION_ERRORS=$((VALIDATION_ERRORS + 1))
    fi
done

# Check for gameinfo.txt
if [ ! -f "$SD_CARD_PATH/xash3d/valve/gameinfo.txt" ]; then
    echo -e "${YELLOW}Warning: gameinfo.txt not found${NC}"
    VALIDATION_ERRORS=$((VALIDATION_ERRORS + 1))
fi

# Report validation results
echo ""
if [ $VALIDATION_ERRORS -eq 0 ]; then
    echo -e "${GREEN}Asset validation passed!${NC}"
else
    echo -e "${YELLOW}Asset validation completed with $VALIDATION_ERRORS warning(s)${NC}"
fi

# Summary
echo ""
echo "=== Staging Summary ==="
echo "Source: $SOURCE_DIR"
echo "Target: $SD_CARD_PATH"
echo "Assets copied to: $SD_CARD_PATH/xash3d/valve/"
echo ""
echo "SD card layout:"
echo "  sd:/apps/xash3d-gc/boot.dol"
echo "  sd:/xash3d/valve/"
echo "  sd:/xash3d/valve/save/"
echo "  sd:/xash3d/valve/logs/"
echo "  sd:/xash3d/valve/screenshots/"
echo ""
echo "To deploy to GameCube:"
echo "  1. Copy boot.dol to sd:/apps/xash3d-gc/boot.dol"
echo "  2. Insert SD card into GameCube"
echo "  3. Boot through Swiss loader or similar"
echo ""
echo -e "${GREEN}Asset staging complete!${NC}"