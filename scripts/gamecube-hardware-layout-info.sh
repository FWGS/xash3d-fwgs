#!/bin/bash
# GameCube Hardware Layout Info
# Prints exact file placement instructions for various hardware routes.

set -e

ROUTE="${1:-all}"

echo "=== Xash3D GameCube Hardware Layout Info ==="
echo ""

if [[ "$ROUTE" == "sd" || "$ROUTE" == "all" ]]; then
    echo "--- Route: SD2SP2 / SD Gecko (DOL Boot) ---"
    echo "1. Insert a FAT32-formatted SD card."
    echo "2. Copy OUT/bin/boot.dol to the root of the SD card:"
    echo "   cp OUT/bin/boot.dol /path/to/mounted/sdcard/"
    echo "3. (Optional) Copy game assets if not using a Disc:"
    echo "   mkdir -p /path/to/mounted/sdcard/xash3d/valve"
    echo "   cp -r Half-Life/valve/* /path/to/mounted/sdcard/xash3d/valve/"
    echo "4. Configure your loader (e.g., Swiss) to boot boot.dol from SD."
    echo ""
fi

if [[ "$ROUTE" == "disc" || "$ROUTE" == "all" ]]; then
    echo "--- Route: GameCube Disc (ISO Boot) ---"
    echo "1. Generate the ISO image:"
    echo "   scripts/build-gamecube-disc.py --smoke-map c0a0e"
    echo "2. Burn OUT/xash3d-gc.iso to a GameCube-compatible disc."
    echo "   - Use DAO (Disc At Once) mode."
    echo "   - Ensure the disc is finalized."
    echo "3. Insert the disc into the GameCube."
    echo "4. Power on the console to boot from DVD."
    echo ""
fi

if [[ "$ROUTE" == "memcard" || "$ROUTE" == "all" ]]; then
    echo "--- Route: Memory Card (DOL Boot) ---"
    echo "1. Prepare a GameCube Memory Card (via adapter or native)."
    echo "2. Copy OUT/bin/boot.dol to the root of the Memory Card."
    echo "3. Note: Memory Cards are small (typically 8MB). You cannot store"
    echo "   full game assets here. Use this route for the loader/boot,"
    echo "   but ensure assets are accessible via SD or Disc."
    echo ""
fi

echo "=== End of Layout Info ==="
