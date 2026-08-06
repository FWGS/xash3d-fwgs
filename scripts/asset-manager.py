#!/usr/bin/env python3
"""
asset-manager.py - Unified asset management for Xash3D GameCube

This script combines asset root discovery and SD staging for Xash3D GameCube.
It automatically discovers asset roots and stages assets to SD card.

Usage:
    python3 asset-manager.py <command> [options]

Commands:
    discover    Discover asset roots
    stage       Stage assets to SD card
    validate    Validate asset structure
    help        Show this help message

Examples:
    python3 asset-manager.py discover
    python3 asset-manager.py stage --source /path/to/Half-Life --sd /media/user/SDCARD
    python3 asset-manager.py validate --sd /media/user/SDCARD

Environment Variables:
    XASH3D_GC_ASSET_ROOT  Root directory for GameCube assets
    XASH3D_GC_VALVE_DIR   Valve directory path
"""

import os
import sys
import shutil
import subprocess
from pathlib import Path
from typing import Optional, List, Dict

_WAIFULIB = Path(__file__).resolve().parent / "waifulib"
if str(_WAIFULIB) not in sys.path:
	sys.path.insert(0, str(_WAIFULIB))
try:
	from gamecube_storage import (  # type: ignore
		FAT_VOLUME_ROOTS,
		format_layout_help,
		strip_device_prefix,
		writable_layout_paths,
	)
except ImportError:
	FAT_VOLUME_ROOTS = ("sd:/", "carda:/", "cardb:/")

	def strip_device_prefix(path: str) -> str:
		for prefix in ("sd:/", "carda:/", "cardb:/", "gcdisc:/", "gcprobe:/"):
			if path.startswith(prefix):
				return path[len(prefix):]
		return path

	def format_layout_help(volume_root: str = "sd:/") -> str:
		return f"layout under {volume_root}xash3d/valve/"

	def writable_layout_paths(volume_root: str):
		return [f"{volume_root}xash3d/valve"]


def print_usage():
    """Print usage information."""
    print(__doc__)


def discover_asset_roots() -> List[Dict[str, str]]:
    """Discover asset roots using environment variables and default paths."""
    roots = []
    
    # Check environment variables first
    asset_root = os.environ.get('XASH3D_GC_ASSET_ROOT')
    if asset_root:
        roots.append({
            'type': 'env_asset_root',
            'path': asset_root,
            'priority': 1
        })
    
    valve_dir = os.environ.get('XASH3D_GC_VALVE_DIR')
    if valve_dir:
        roots.append({
            'type': 'env_valve_dir',
            'path': valve_dir,
            'priority': 2
        })
    
    # Check common default locations (Swiss libdvm volumes + disc)
    default_locations = [
        '/media/user/SDCARD/xash3d/valve',
        '/mnt/sdcard/xash3d/valve',
        'sd:/xash3d/valve',
        'carda:/xash3d/valve',
        'cardb:/xash3d/valve',
        'gcdisc:/xash3d/valve',
    ]
    
    for i, loc in enumerate(default_locations):
        roots.append({
            'type': 'default',
            'path': loc,
            'priority': 3 + i
        })
    
    return roots


def find_valid_asset_root() -> Optional[str]:
    """Find the first valid asset root from discovered roots."""
    roots = discover_asset_roots()
    
    # Sort by priority
    roots.sort(key=lambda x: x['priority'])
    
    for root in roots:
        path = root['path']
        # Remove Swiss device prefixes for local filesystem checking
        local_path = strip_device_prefix(path)
        
        if os.path.exists(local_path):
            print(f"Found valid asset root: {path} (type: {root['type']})")
            return path
    
    return None


def validate_asset_structure(sd_card_path: Path) -> bool:
    """Validate asset structure on SD card."""
    required_dirs = ['models', 'sound', 'materials', 'maps']
    required_files = ['gameinfo.txt']
    
    valve_path = sd_card_path / 'xash3d' / 'valve'
    
    if not valve_path.exists():
        print(f"Error: Valve directory not found: {valve_path}")
        return False
    
    validation_errors = 0
    
    # Check required directories or PK3s
    for dir_name in required_dirs:
        dir_path = valve_path / dir_name
        pk3_path = valve_path / f"{dir_name}.pk3"
        
        if not dir_path.exists() and not pk3_path.exists():
            print(f"Warning: Required directory or PK3 not found: {dir_name}")
            validation_errors += 1
    
    # Check required files
    for file_name in required_files:
        file_path = valve_path / file_name
        if not file_path.exists():
            print(f"Warning: Required file not found: {file_name}")
            validation_errors += 1
    
    if validation_errors == 0:
        print("Asset structure validation passed!")
        return True
    else:
        print(f"Asset structure validation completed with {validation_errors} warning(s)")
        return True  # Don't fail on warnings


def create_pk3(source_dir: Path, output_dir: Path, dir_name: str) -> bool:
    """Create a PK3 archive from a directory using makepak.py."""
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
    
    if validate_asset_structure(sd_card_path):
        # Summary
        print("")
        print("=== Staging Summary ===")
        print(f"Source: {source_dir}")
        print(f"Target: {sd_card_path}")
        print(f"Assets copied to: {sd_card_path}/xash3d/valve/")
        print("")
        print("SD / Swiss FAT layout (sd: SD2SP2; also carda:/cardb: for SD Gecko):")
        for line in format_layout_help("sd:/").splitlines():
            print(f"  {line}" if not line.startswith("==") else line)
        print("")
        print("To deploy to GameCube via Swiss:")
        print("  1. Copy boot.dol onto media Swiss can browse")
        print("  2. Place valve assets under sd:/xash3d/valve/ (or carda:/ / cardb:/)")
        print("  3. Launch boot.dol from Swiss")
        print("")
        print("Asset staging complete!")
        return True
    else:
        print("Asset staging failed!")
        return False


def cmd_discover(args):
    """Handle 'discover' command."""
    print("Discovering asset roots...")
    print("")
    
    roots = discover_asset_roots()
    
    if not roots:
        print("No asset roots discovered.")
        return 1
    
    print("Discovered asset roots:")
    for root in roots:
        print(f"  [{root['type']}] {root['path']} (priority: {root['priority']})")
    
    valid_root = find_valid_asset_root()
    if valid_root:
        print(f"\nValid asset root: {valid_root}")
        return 0
    else:
        print("\nNo valid asset root found.")
        return 1


def cmd_stage(args):
    """Handle 'stage' command."""
    source_dir = None
    sd_card_path = None
    
    # Parse arguments
    i = 0
    while i < len(args):
        if args[i] == '--source' and i + 1 < len(args):
            source_dir = Path(args[i + 1])
            i += 2
        elif args[i] == '--sd' and i + 1 < len(args):
            sd_card_path = Path(args[i + 1])
            i += 2
        else:
            i += 1
    
    # Use environment variables if not specified
    if not source_dir:
        asset_root = os.environ.get('XASH3D_GC_ASSET_ROOT')
        if asset_root:
            source_dir = Path(asset_root) / 'valve'
    
    if not sd_card_path:
        # Try to find SD card automatically
        sd_card_path = Path('/media/user/SDCARD')
        if not sd_card_path.exists():
            sd_card_path = Path('/mnt/sdcard')
    
    if not source_dir or not sd_card_path:
        print("Error: Missing required arguments")
        print("Usage: python3 asset-manager.py stage --source <path> --sd <path>")
        return 1
    
    success = stage_assets(source_dir, sd_card_path)
    return 0 if success else 1


def cmd_validate(args):
    """Handle 'validate' command."""
    sd_card_path = None
    
    # Parse arguments
    i = 0
    while i < len(args):
        if args[i] == '--sd' and i + 1 < len(args):
            sd_card_path = Path(args[i + 1])
            i += 2
        else:
            i += 1
    
    if not sd_card_path:
        # Try to find SD card automatically
        sd_card_path = Path('/media/user/SDCARD')
        if not sd_card_path.exists():
            sd_card_path = Path('/mnt/sdcard')
    
    if not sd_card_path:
        print("Error: Missing required arguments")
        print("Usage: python3 asset-manager.py validate --sd <path>")
        return 1
    
    success = validate_asset_structure(sd_card_path)
    return 0 if success else 1


def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print_usage()
        sys.exit(1)
    
    command = sys.argv[1]
    args = sys.argv[2:]
    
    if command == 'help' or command == '--help' or command == '-h':
        print_usage()
        sys.exit(0)
    elif command == 'discover':
        sys.exit(cmd_discover(args))
    elif command == 'stage':
        sys.exit(cmd_stage(args))
    elif command == 'validate':
        sys.exit(cmd_validate(args))
    else:
        print(f"Unknown command: {command}")
        print_usage()
        sys.exit(1)


if __name__ == "__main__":
    main()