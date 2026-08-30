#!/usr/bin/env python3
"""
stage_sd_assets.py - Stage Half-Life assets to SD card for GameCube

This script helps prepare assets for deployment to a GameCube SD card
(or SD Gecko volume). It creates PK3 archives and validates asset structure.

Usage:
    python3 stage_sd_assets.py <source_dir> <sd_card_path>

Example:
    python3 stage_sd_assets.py /home/tim/Desktop/xash3d-gc/Half-Life /media/user/SDCARD

The script will:
1. Copy assets from source to SD card
2. Create PK3 archives for efficient loading
3. Validate asset structure and report issues
"""

import os
import sys
import shutil
from pathlib import Path

_WAIFULIB = Path(__file__).resolve().parent / "waifulib"
if str(_WAIFULIB) not in sys.path:
	sys.path.insert(0, str(_WAIFULIB))
try:
	from gamecube_storage import format_layout_help
except ImportError:
	def format_layout_help(volume_root: str = "sd:/") -> str:
		return f"{volume_root}xash3d/valve/"


def print_usage():
	"""Print usage information."""
	print("Usage: python3 stage_sd_assets.py <source_dir> <sd_card_path>")
	print("")
	print("Arguments:")
	print("  source_dir    Path to Half-Life assets (e.g., /path/to/Half-Life)")
	print("  sd_card_path  Path to SD card mount point (e.g., /media/user/SDCARD)")
	print("")
	print("Example:")
	print("  python3 stage_sd_assets.py /home/tim/Desktop/xash3d-gc/Half-Life /media/user/SDCARD")
	print("")
	print("The script will:")
	print("  1. Copy assets to sd:/xash3d/valve/ (or carda:/ / cardb:/ on SD Gecko)")
	print("  2. Create PK3 archives for efficient loading")
	print("  3. Validate asset structure")
	print("")
	print(format_layout_help("sd:/"))
	print("")
	print("Also supported on Swiss/libdvm: carda:/ and cardb:/ (SD Gecko).")


def create_pk3(source_dir: Path, output_dir: Path, dir_name: str) -> bool:
    """Create a PK3 archive from a directory using makepak.py."""
    import subprocess
    
    source_path = source_dir / dir_name
    if not source_path.exists():
        print(f"  Skipping {dir_name} (not found)")
        return False
    
    # Create temp directory for staging
    temp_dir = source_dir / f".temp_{dir_name}"
    temp_dir.mkdir(exist_ok=True)
    
    # Copy files to temp directory
    for item in source_path.iterdir():
        if item.is_file():
            shutil.copy2(item, temp_dir / item.name)
    
    # Create PK3 using makepak.py
    script_dir = Path(__file__).parent
    makepak_script = script_dir / "makepak.py"
    
    pk3_name = f"{dir_name}.pk3"
    pk3_path = output_dir / pk3_name
    
    try:
        result = subprocess.run(
            [sys.executable, str(makepak_script), str(temp_dir), str(pk3_path)],
            capture_output=True,
            text=True
        )
        
        if result.returncode != 0:
            print(f"  Error creating {pk3_name}: {result.stderr}")
            return False
        
        print(f"  Created {pk3_name}")
        return True
        
    finally:
        # Clean up temp directory
        shutil.rmtree(temp_dir, ignore_errors=True)


def stage_assets(source_dir: Path, sd_card_path: Path) -> bool:
    """Stage assets to SD card."""
    
    print(f"Staging assets to SD card...")
    print(f"Source: {source_dir}")
    print(f"Target: {sd_card_path}")
    print("")
    
    # Validate source directory
    if not source_dir.exists():
        print(f"Error: Source directory does not exist: {source_dir}")
        return False
    
    # Validate SD card path
    if not sd_card_path.exists():
        print(f"Error: SD card path does not exist: {sd_card_path}")
        return False
    
    # Check if SD card is writable
    if not os.access(sd_card_path, os.W_OK):
        print(f"Warning: SD card may not be writable: {sd_card_path}")
        response = input("Continue anyway? (y/N) ")
        if response.lower() != 'y':
            return False
    
    # Create SD card directory structure
    print("Creating SD card directory structure...")
    (sd_card_path / "xash3d" / "valve" / "save").mkdir(parents=True, exist_ok=True)
    (sd_card_path / "xash3d" / "valve" / "logs").mkdir(parents=True, exist_ok=True)
    (sd_card_path / "xash3d" / "valve" / "screenshots").mkdir(parents=True, exist_ok=True)
    (sd_card_path / "apps" / "xash3d-gc").mkdir(parents=True, exist_ok=True)
    
    print("Directory structure created.")
    
    # Copy assets to SD card
    print("")
    print("Copying assets to SD card...")
    
    # List of directories to copy
    asset_dirs = ["models", "sound", "materials", "maps", "scripts", "cfg", "resource", "fonts", "particles"]
    
    for dir_name in asset_dirs:
        source_path = source_dir / dir_name
        target_path = sd_card_path / "xash3d" / "valve" / dir_name
        
        if source_path.exists():
            print(f"  Copying {dir_name}...")
            if target_path.exists():
                shutil.rmtree(target_path)
            shutil.copytree(source_path, target_path)
        else:
            print(f"  Skipping {dir_name} (not found)")
    
    # Copy specific files
    print("Copying specific files...")
    for file_name in ["gameinfo.txt", "halflife.gcd", "valve.gcd"]:
        source_file = source_dir / file_name
        target_file = sd_card_path / "xash3d" / "valve" / file_name
        
        if source_file.exists():
            print(f"  Copying {file_name}...")
            shutil.copy2(source_file, target_file)
    
    # Create PK3 archives for efficient loading
    print("")
    print("Creating PK3 archives...")
    
    pk3_dirs = ["models", "sound", "materials", "maps", "scripts", "cfg"]
    for dir_name in pk3_dirs:
        create_pk3(source_dir, sd_card_path / "xash3d" / "valve", dir_name)
    
    # Validate asset structure
    print("")
    print("Validating asset structure...")
    
    validation_errors = 0
    
    # Check for required directories or PK3s
    for dir_name in ["models", "sound", "materials", "maps"]:
        source_path = source_dir / dir_name
        pk3_path = sd_card_path / "xash3d" / "valve" / f"{dir_name}.pk3"
        
        if not source_path.exists() and not pk3_path.exists():
            print(f"Warning: Required directory or PK3 not found: {dir_name}")
            validation_errors += 1
    
    # Check for gameinfo.txt
    gameinfo_path = sd_card_path / "xash3d" / "valve" / "gameinfo.txt"
    if not gameinfo_path.exists():
        print("Warning: gameinfo.txt not found")
        validation_errors += 1
    
    # Report validation results
    print("")
    if validation_errors == 0:
        print("Asset validation passed!")
    else:
        print(f"Asset validation completed with {validation_errors} warning(s)")
    
    # Summary
    print("")
    print("=== Staging Summary ===")
    print(f"Source: {source_dir}")
    print(f"Target: {sd_card_path}")
    print(f"Assets copied to: {sd_card_path}/xash3d/valve/")
    print("")
    print("Swiss FAT layout (sd: SD2SP2; carda:/cardb: SD Gecko):")
    for line in format_layout_help("sd:/").splitlines():
        print(f"  {line}" if not line.startswith("==") else line)
    print("")
    print("To deploy via Swiss:")
    print("  1. Copy boot.dol onto media Swiss can browse")
    print("  2. Place valve assets under sd:/xash3d/valve/ (or carda:/ / cardb:/)")
    print("  3. Launch boot.dol from Swiss")
    print("")
    print("Asset staging complete!")
    
    return True


def main():
    """Main entry point."""
    if len(sys.argv) < 3:
        print_usage()
        sys.exit(1)
    
    source_dir = Path(sys.argv[1])
    sd_card_path = Path(sys.argv[2])
    
    success = stage_assets(source_dir, sd_card_path)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()