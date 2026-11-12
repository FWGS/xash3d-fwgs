#!/usr/bin/env python3
"""Build a bootable GameCube disc image for local Xash3D testing."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

# Sibling helpers live under scripts/ (bake_capfaces_pack, etc.).
_SCRIPTS_DIR = str(Path(__file__).resolve().parent)
if _SCRIPTS_DIR not in sys.path:
	sys.path.insert(0, _SCRIPTS_DIR)

# Disc assembly compiles a small apploader and stages several temporary asset
# trees.  On the development host /tmp may be quota-limited even when the
# workspace has ample room; prefer a tmpfs unless the caller selected a path.
if not os.environ.get("TMPDIR") and os.path.isdir("/dev/shm"):
	os.environ["TMPDIR"] = "/dev/shm"
	tempfile.tempdir = "/dev/shm"

# Critical assets required for basic engine boot and map loading
CRITICAL_ASSETS = (
	"liblist.gam",
	"delta.lst",
	"gfx.wad",
    "gfx/palette.lmp",
    "gfx/conback.lmp",
    "gfx/colormap.lmp",
    "valve.rc",
    "default.cfg",
    "config.cfg",
)

# Assets that are either too large for GC memory budget or unsupported formats
UNSUPPORTED_EXTENSIONS = {
    ".wmv", ".mpg", ".mpeg",          # Video: unsupported or too heavy
    ".mp3", ".ogg",                   # Audio: streaming not yet stable on GC
}

# Max size for any single asset (10MB). GC has 24MB total RAM. 
# Loading many large assets will OOM during map load.
MAX_ASSET_SIZE = 10 * 1024 * 1024  

# Directories where we strictly enforce lowercase naming for case-insensitive FS compatibility
STRICT_CASE_DIRS = (
    "maps",
    "models",
    "sprites",
    "decals",
    "sound",
    "gfx",
    "lang",
)


DISC_HEADER_SIZE = 0x3000
DISC_MAGIC = 0xC2339F3D
APPLOADER_ADDRESS = 0x81200000
APPLOADER_HEADER_OFFSET = 0x2440
APPLOADER_DATA_OFFSET = APPLOADER_HEADER_OFFSET + 0x20
BOOTSTRAP_EXCLUDED_EXTENSIONS = {".avi", ".gcvid", ".gcpcm", ".mdl", ".pak", ".pk3", ".wad", ".wav"}
GCVID_MAGIC = b"GCV2"
GCVID_HEADER_SIZE = 28
# Half-res static-hold companions — matches runtime intro decode/upload scale
# and the 320x240 cinematic BSS texture on GameCube.
GCVID_WIDTH = 320
GCVID_HEIGHT = 240
GCVID_FPS_NUM = 15
GCVID_FPS_DEN = 1
GCVID_TILE_SIZE = 8
GCVID_FLAG_STATIC_HOLD = 1 << 31
GCVID_FLAG_BGRA32 = 1 << 30
GCVID_KEYFRAME = 0
GCVID_DELTAFRAME = 1
GCVID_STILL_FRAME_INDEX = 80
GCVID_LOGO_WIDTH = 320
GCVID_LOGO_HEIGHT = 48
GCVID_LOGO_FPS_NUM = 24
GCVID_LOGO_FPS_DEN = 1
GCVID_LOGO_STILL_FRAME_INDEX = 80
GCPCM_MAGIC = b"GCPA"
GCPCM_RATE = 48000
GCPCM_CHANNELS = 2
GCPCM_WIDTH = 2

GC_BUILD_BANNER = """
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣠⣤⣴⣶⣾⣿⣿⣿⣿⣿⣿⣷⣶⣦⣤⣄⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⣀⣴⣾⣿⣿⣿⣿⣿⣿⣿⠿⠿⠿⠿⢿⣿⣿⣿⣿⣿⣿⣷⣦⣀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⣠⣾⣿⣿⣿⡿⠟⠋⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠙⠻⢿⣿⣿⣿⣷⣄⡀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⣠⣾⣿⣿⣿⠟⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠉⠻⣿⣿⣿⣷⣄⠀⠀⠀⠀
⠀⠀⢀⣼⣿⣿⣿⠟⠁⠀⠀⠀⠀⠀⠀⣶⣶⣶⣶⣶⣶⣦⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠻⣿⣿⣿⣧⡀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
"""


def align(value: int, boundary: int) -> int:
	return (value + boundary - 1) & ~(boundary - 1)


@dataclass
class Node:
	name: str
	source: Path | None = None
	children: dict[str, "Node"] = field(default_factory=dict)
	index: int = 0
	parent_index: int = 0
	next_index: int = 0
	name_offset: int = 0
	disc_offset: int = 0

	@property
	def is_dir(self) -> bool:
		return self.source is None


def add_tree(parent: Node, source: Path) -> None:
	for child in sorted(source.iterdir(), key=lambda path: path.name.lower()):
		if child.is_symlink():
			raise ValueError(f"symlinks are not supported: {child}")
		if child.is_dir():
			node = Node(child.name)
			parent.children[child.name] = node
			add_tree(node, child)
		elif child.is_file():
			parent.children[child.name] = Node(child.name, child)


def flatten(node: Node, parent_index: int, entries: list[Node]) -> None:
	node.index = len(entries)
	node.parent_index = parent_index
	entries.append(node)

	for child in sorted(node.children.values(), key=lambda item: (not item.is_dir, item.name.lower())):
		if child.is_dir:
			flatten(child, node.index, entries)
		else:
			child.index = len(entries)
			child.parent_index = node.index
			entries.append(child)

	node.next_index = len(entries)


def encode_names(entries: list[Node]) -> bytes:
	names = bytearray()
	for entry in entries:
		encoded = entry.name.encode("utf-8")
		if b"\0" in encoded:
			raise ValueError(f"invalid filename: {entry.name!r}")
		entry.name_offset = len(names)
		names.extend(encoded)
		names.append(0)
	if len(names) >= 1 << 24:
		raise ValueError("FST name table is too large")
	return bytes(names)


def build_fst(entries: list[Node], names: bytes) -> bytes:
	fst = bytearray(len(entries) * 12)
	for entry in entries:
		name_word = entry.name_offset
		if entry.is_dir:
			name_word |= 0x01000000
			second = entry.parent_index
			third = entry.next_index
		else:
			second = entry.disc_offset
			third = entry.source.stat().st_size
		struct.pack_into(">III", fst, entry.index * 12, name_word, second, third)
	return bytes(fst) + names


def write_padding(output, target: int) -> None:
	remaining = target - output.tell()
	if remaining < 0:
		raise ValueError("disc layout overlaps")
	if remaining:
		output.write(b"\0" * remaining)


def find_tool(name: str) -> str:
	devkitpro = Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))
	candidate = devkitpro / "devkitPPC" / "bin" / f"powerpc-eabi-{name}"
	if candidate.is_file():
		return str(candidate)
	found = shutil.which(f"powerpc-eabi-{name}")
	if found is None:
		raise FileNotFoundError(f"powerpc-eabi-{name} was not found")
	return found


def parse_dol(dol: Path) -> tuple[list[tuple[int, int, int]], int, int, int]:
	header = dol.read_bytes()[:0xE4]
	if len(header) != 0xE4:
		raise ValueError(f"invalid DOL header: {dol}")
	sections: list[tuple[int, int, int]] = []
	for count, offset_base, address_base, size_base in (
		(7, 0x00, 0x48, 0x90),
		(11, 0x1C, 0x64, 0xAC),
	):
		for index in range(count):
			offset = struct.unpack_from(">I", header, offset_base + index * 4)[0]
			address = struct.unpack_from(">I", header, address_base + index * 4)[0]
			size = struct.unpack_from(">I", header, size_base + index * 4)[0]
			if size:
				sections.append((offset, address, size))
	bss_address, bss_size, entry_point = struct.unpack_from(">III", header, 0xD8)
	return sections, bss_address, bss_size, entry_point


def build_apploader(
	source: Path,
	linker_script: Path,
	dol: Path,
	dol_offset: int,
	fst_offset: int,
	fst_size: int,
) -> bytes:
	with tempfile.TemporaryDirectory(prefix="xash3d-gc-apploader-") as temp:
		temp_path = Path(temp)
		section_header = temp_path / "gamecube-apploader-sections.h"
		obj = temp_path / "apploader.o"
		elf = temp_path / "apploader.elf"
		binary = temp_path / "apploader.bin"
		gcc = find_tool("gcc")
		objcopy = find_tool("objcopy")
		sections, bss_address, bss_size, entry_point = parse_dol(dol)
		sections = [
			(dol_offset + offset, address, size) for offset, address, size in sections
		]
		fst_address = 0x81700000
		sections.append((fst_offset, fst_address, fst_size))
		section_lines = ",\n".join(
			f"\t{{ 0x{offset:08x}u, 0x{address:08x}u, 0x{size:08x}u }}"
			for offset, address, size in sections
		)
		section_header.write_text(
			f"#define APPLOADER_SECTION_COUNT {len(sections)}\n"
			f"#define APPLOADER_BSS_ADDRESS 0x{bss_address:08x}u\n"
			f"#define APPLOADER_BSS_SIZE 0x{bss_size:08x}u\n"
			f"#define APPLOADER_ENTRY_POINT 0x{entry_point:08x}u\n"
			f"#define APPLOADER_FST_ADDRESS 0x{fst_address:08x}u\n"
			f"#define APPLOADER_FST_SIZE 0x{fst_size:08x}u\n"
			"static const apploader_section_t apploader_sections[] =\n{\n"
			f"{section_lines}\n}};\n",
			encoding="ascii",
		)

		subprocess.run([
			gcc, "-c", str(source), "-o", str(obj), "-Os", "-mcpu=750",
			"-m32", "-mhard-float", "-ffreestanding", "-fno-pic",
			"-ffunction-sections", "-fdata-sections", "-msdata=none", "-I", temp,
		], check=True)
		subprocess.run([
			gcc, str(obj), "-o", str(elf), "-nostdlib", "-nodefaultlibs",
			f"-Wl,-T,{linker_script}", "-Wl,--gc-sections",
		], check=True)
		subprocess.run([objcopy, "-O", "binary", str(elf), str(binary)], check=True)
		result = binary.read_bytes()

	if len(result) > DISC_HEADER_SIZE - APPLOADER_DATA_OFFSET:
		raise ValueError(f"apploader is too large: {len(result)} bytes")
	return result


def write_bootstrap_pk3(
	data: Path,
	bootstrap: Path,
	recursive: bool = False,
	extra_media: list[Path] | None = None,
) -> None:
	"""Write gamecube-bootstrap.pk3 (small files plus GC studio/HUD/font/sky mirrors).

	Shared by the disc image and the Swiss SD card stage so both routes boot
	the same payload. extra_media: CapFaces packs / sky CMPR from overlays —
	ISO9660 directory find misses these on some grafts (charlie exists=0).
	"""
	with zipfile.ZipFile(bootstrap, "w", zipfile.ZIP_DEFLATED) as archive:
		children = data.rglob("*") if recursive else data.iterdir()
		for child in sorted(children):
			suffix = child.suffix.lower()
			# Keep the startup playlist as a loose ISO file. Retail boot overlays
			# replace it per build (Valve intro enabled or intentionally skipped),
			# while the bootstrap archive is mounted with higher search priority.
			# Copying an older empty playlist here silently masks the overlay.
			if child.relative_to(data).as_posix().lower() == "media/startupvids.txt":
				continue
			# G346: smoke changelevel dests like c1a0.bsp are ~2.5 MiB. The
			# old 2 MiB cap left them FST-only; when directory find flaps to
			# pk3-only (c0a0e→c1a0 20260810-195734/195827) Host_Error misses
			# the map. Allow BSPs up to 4 MiB in the bootstrap archive.
			# Spatial *cells* (cf_*_sXX.gcp) are small and must ride bootstrap
			# when ISO9660 misses the parent *_spatial.pk3 (escape). Keep the
			# fat parent pk3 out of bootstrap — it tip-starves escape textures.
			name_l = child.name.lower()
			size_cap = 4 * 1024 * 1024 if suffix == ".bsp" else 2 * 1024 * 1024
			if name_l.endswith("_spatial.pk3"):
				continue
			# World DL packs stay loose on the ISO — inflateInit2 OOMs on 1+ MiB
			# .gcd entries when the bootstrap pk3 is the only search hit.
			if name_l.startswith("wd_") and name_l.endswith(".gcd"):
				continue
			if (
				child.is_file()
				and suffix not in BOOTSTRAP_EXCLUDED_EXTENSIONS
				and child.stat().st_size <= size_cap
			):
				compress_type = (
					zipfile.ZIP_STORED
					if suffix in (".bsp", ".mdl")
					else zipfile.ZIP_DEFLATED
				)
				archive.write(
					child,
					child.relative_to(data).as_posix(),
					compress_type=compress_type,
				)
		# The fallback menu is loaded through the bootstrap search path during
		# read-only retail startup. Keep generated GC menu tiles there even when
		# the general bootstrap is intentionally non-recursive.
		menu_dir = data / "resource" / "gc_menu"
		if menu_dir.is_dir() and not recursive:
			for child in sorted(menu_dir.rglob("*")):
				if child.is_file() and child.suffix.lower() not in BOOTSTRAP_EXCLUDED_EXTENSIONS:
					archive.write(child, child.relative_to(data).as_posix(), compress_type=zipfile.ZIP_DEFLATED)
		studio_count = inject_gc_studio_into_bootstrap(archive, data)
		if studio_count:
			print(f"GameCube studio mirror: injected {studio_count} MDL(s) into bootstrap pk3")
		hud_count = inject_gc_hud_into_bootstrap(archive, data)
		if hud_count:
			print(f"GameCube HUD mirror: injected {hud_count} sprite(s) into bootstrap pk3")
		font_count = inject_gc_font_into_bootstrap(archive, data)
		if font_count:
			print(f"GameCube font mirror: injected {font_count} lean font(s) into bootstrap pk3")
		sky_count = inject_gc_sky_into_bootstrap(archive, data)
		if sky_count:
			print(f"GameCube sky mirror: injected {sky_count} lean BMP(s) into bootstrap pk3")
		extra_n = 0
		for path in extra_media or []:
			if not path.is_file():
				continue
			name = path.name.lower()
			# Tip-safe bootstrap: primary cf_<map>.gcp + skies only.
			# Spatial cells stay as loose ISO media/ (ship unpacks the pk3);
			# injecting 60+ cells tip-starved escape before densify.
			is_sky = name.startswith("sky_") and name.endswith(".cmp")
			is_primary_cf = (
				name.startswith("cf_")
				and name.endswith(".gcp")
				and "_s" not in name[3:-4]
			)
			if not is_sky and not is_primary_cf:
				continue
			if path.stat().st_size <= 0 or path.stat().st_size > 2 * 1024 * 1024:
				continue
			arcname = f"media/{name}"
			if arcname in archive.NameToInfo:
				continue
			archive.write(path, arcname, compress_type=zipfile.ZIP_STORED)
			extra_n += 1
		if extra_n:
			print(f"GameCube media mirror: injected {extra_n} overlay CapFaces/sky file(s) into bootstrap pk3")
		# G293 disc inject deferred — ISO early-exit while debugging tip-safe path.


def _safe_gamedir_name(name: str) -> str:
	cleaned = (name or "").strip()
	if not cleaned or any(c for c in cleaned if not (c.isalnum() or c in "_-")):
		raise ValueError(f"invalid gamedir name: {name!r}")
	return cleaned


def discover_sibling_gamedirs(primary_data: Path, primary_gamedir: str) -> list[tuple[str, Path]]:
	"""Find sibling GoldSrc trees next to the primary --data folder (e.g. dod beside valve)."""
	known = ("valve", "dod", "gearbox", "bshift", "cstrike", "czero", "tfc", "dmc", "ricochet")
	parent = primary_data.resolve().parent
	found: list[tuple[str, Path]] = []
	for name in known:
		if name == primary_gamedir:
			continue
		candidate = parent / name
		if not candidate.is_dir():
			continue
		if (candidate / "liblist.gam").is_file() or (candidate / "gfx.wad").is_file():
			found.append((name, candidate))
	return found


def parse_extra_data_args(values: list[str] | None) -> list[tuple[str, Path]]:
	"""Parse --extra-data GAMEDIR=PATH entries."""
	result: list[tuple[str, Path]] = []
	for raw in values or []:
		if "=" not in raw:
			raise ValueError(f"--extra-data expects GAMEDIR=PATH, got {raw!r}")
		name, path_str = raw.split("=", 1)
		gamedir = _safe_gamedir_name(name)
		path = Path(path_str)
		if not path.is_dir():
			raise FileNotFoundError(f"--extra-data path missing for {gamedir}: {path}")
		result.append((gamedir, path))
	return result


def build_iso9660(
	data: Path,
	extras: Path | None,
	output_path: Path,
	bootstrap_recursive: bool = False,
	overlays: tuple[tuple[str, Path], ...] = (),
	gamedir: str = "valve",
	extra_gamedirs: tuple[tuple[str, Path], ...] = (),
) -> None:
	xorriso = shutil.which("xorriso")
	if xorriso is None:
		raise FileNotFoundError("xorriso is required to build the GameCube data disc")

	primary = _safe_gamedir_name(gamedir)
	command = [
		xorriso,
		"-as", "mkisofs",
		"-iso-level", "3",
		"-J",
		"-R",
		"-V", "XASH3D_GC",
		"-o", str(output_path),
		"-graft-points",
		f"/xash3d/{primary}={data}",
	]
	seen = {primary}
	for extra_name, extra_path in extra_gamedirs:
		name = _safe_gamedir_name(extra_name)
		if name in seen:
			continue
		seen.add(name)
		command.append(f"/xash3d/{name}={extra_path}")
		print(f"GameCube ISO: extra gamedir graft /xash3d/{name}={extra_path}")

	with tempfile.TemporaryDirectory(prefix="xash3d-gc-bootstrap-") as temp:
		bootstrap = Path(temp) / "gamecube-bootstrap.pk3"
		boot_media = [
			src
			for rel, src in overlays
			if isinstance(rel, str)
			and (
				rel.startswith("media/cf_")
				or rel.startswith("media/sky_")
				or rel.endswith("_spatial.pk3")
			)
		]
		write_bootstrap_pk3(
			data, bootstrap, bootstrap_recursive, extra_media=boot_media
		)

		if extras is not None:
			command.append(f"/xash3d/{primary}/extras.pk3={extras}")
		for relative, source in overlays:
			command.append(f"/xash3d/{primary}/{relative}={source}")
		command.append(f"/xash3d/{primary}/gamecube-bootstrap.pk3={bootstrap}")
		print(
			f"GameCube ISO: assembling {len(command) - 11} grafts into "
			f"{output_path} (primary={primary}, xorriso progress follows)",
			flush=True,
		)
		output_path.unlink(missing_ok=True)
		subprocess.run(command, check=True)


SMOKE_CONFIG_FILES = (
	"liblist.gam",
	"delta.lst",
	"default.cfg",
	"config.cfg",
	"autoexec.cfg",
	"game.cfg",
	"language.cfg",
	"listenserver.cfg",
	"joystick.cfg",
	"server.cfg",
	"skill.cfg",
	"spserver.cfg",
	"sound/sentences.txt",
	"valve.rc",
	"xashcomm.lst",
	"gfx/colormap.lmp",
	"gfx/conback.lmp",
	"gfx/palette.lmp",
)

SMOKE_INTRO_MEDIA = (
	"media/sierra.avi",
	"media/valve.avi",
)

MENU_RESOURCE_ASSETS = (
	"resource/logo_game.tga",
	"resource/BackgroundLayout.txt",
	"resource/HD_BackgroundLayout.txt",
	"resource/menu_hl_no_icon.tga",
	"resource/game_menu.tga",
	"resource/game_menu_mouseover.tga",
	"resource/background/800_1_a_loading.tga",
	"resource/background/800_1_b_loading.tga",
	"resource/background/800_1_c_loading.tga",
	"resource/background/800_1_d_loading.tga",
	"resource/background/800_2_a_loading.tga",
	"resource/background/800_2_b_loading.tga",
	"resource/background/800_2_c_loading.tga",
	"resource/background/800_2_d_loading.tga",
	"resource/background/800_3_a_loading.tga",
	"resource/background/800_3_b_loading.tga",
	"resource/background/800_3_c_loading.tga",
	"resource/background/800_3_d_loading.tga",
)

MENU_RESOURCE_DIRS = (
	"resource",
	"gfx/shell",
)


SMOKE_HUD_RES = 320

SMOKE_HUD_SPRITES = (
	"sprites/animglow01.spr",
	"sprites/camera.spr",
	"sprites/crosshairs.spr",
	"sprites/dot.spr",
	"sprites/hud.txt",
	"sprites/iplayer.spr",
	"sprites/iplayerblue.spr",
	"sprites/iplayerdead.spr",
	"sprites/iplayerred.spr",
	"sprites/laserbeam.spr",
	"sprites/lgtning.spr",  # G320: reactor electricity / env_beam texture
	"sprites/muzzleflash1.spr",
	"sprites/muzzleflash2.spr",
	"sprites/muzzleflash3.spr",
	"sprites/richo1.spr",
	"sprites/shellchrome.spr",
	"sprites/tile.spr",
	"sprites/voiceicon.spr",
)


def _sprite_txt_sheet_paths(path: Path, res: int) -> set[str]:
	resources: set[str] = set()
	if not path.is_file():
		return resources

	for raw_line in path.read_text(encoding="latin-1", errors="replace").splitlines():
		line = raw_line.strip()
		if not line or line.startswith("//"):
			continue

		parts = line.split()
		if len(parts) < 7:
			continue

		try:
			entry_res = int(parts[1])
		except ValueError:
			continue

		if entry_res != res:
			continue

		sprite_name = parts[2].replace("\\", "/")
		resources.add(f"sprites/{sprite_name}.spr")

	return resources


def smoke_hud_resources(source: Path, res: int = SMOKE_HUD_RES) -> tuple[str, ...]:
	"""Collect HUD and weapon sprite files for the smoke-test resolution."""
	resources: set[str] = set(SMOKE_HUD_SPRITES)
	sprites_dir = source / "sprites"

	hud_txt = sprites_dir / "hud.txt"
	resources.update(_sprite_txt_sheet_paths(hud_txt, res))

	for weapon_txt in sorted(sprites_dir.glob("weapon_*.txt")):
		resources.add(f"sprites/{weapon_txt.name}")
		resources.update(_sprite_txt_sheet_paths(weapon_txt, res))

	# Some HLSDK HUD elements are loaded directly via LoadSprite("sprites/%d_*.spr")
	# instead of being enumerated in hud.txt/weapon_*.txt.
	resources.add(f"sprites/{res}_pain.spr")
	resources.add(f"sprites/{res}_train.spr")
	resources.add(f"sprites/{res}_logo.spr")
	return tuple(sorted(resources))

SMOKE_SOUND_DIRS = (
	"sound/items",
	"sound/player",
	"sound/weapons",
)

SMOKE_PRECACHE_MODELS = (
	"models/grenade.mdl",
	"models/p_357.mdl",
	"models/p_9mmar.mdl",
	"models/p_9mmhandgun.mdl",
	"models/p_crossbow.mdl",
	"models/p_crowbar.mdl",
	"models/p_egon.mdl",
	"models/p_gauss.mdl",
	"models/p_grenade.mdl",
	"models/p_hgun.mdl",
	"models/p_rpg.mdl",
	"models/p_satchel.mdl",
	"models/p_satchel_radio.mdl",
	"models/p_shotgun.mdl",
	"models/p_squeak.mdl",
	"models/p_tripmine.mdl",
	"models/shell.mdl",
	"models/shotgunshell.mdl",
	"models/v_357.mdl",
	"models/v_9mmar.mdl",
	"models/v_9mmhandgun.mdl",
	"models/v_crossbow.mdl",
	"models/v_crowbar.mdl",
	"models/headcrab.mdl",
	"models/zombie.mdl",
	# G371: denser AM static NPCs (scientist/barney) for Flipper studio emit.
	"models/scientist.mdl",
	"models/barney.mdl",
	"models/roach.mdl",
	# c1a1f furniture + campaign cast for studio BSS promote.
	"models/filecabinet.mdl",
	"models/gman.mdl",
	"models/houndeye.mdl",
	"models/forklift.mdl",
	"models/loader.mdl",
	# G376: *t.mdl skins (base MDLs have numtextures=0).
	"models/scientistt.mdl",
	"models/barneyt.mdl",
	"models/headcrabt.mdl",
	"models/zombiet.mdl",
	"models/gmant.mdl",
	"models/houndeyet.mdl",
	"models/v_egon.mdl",
	"models/v_gauss.mdl",
	"models/v_grenade.mdl",
	"models/v_hgun.mdl",
	"models/v_rpg.mdl",
	"models/v_satchel.mdl",
	"models/v_satchel_radio.mdl",
	"models/v_shotgun.mdl",
	"models/v_squeak.mdl",
	"models/v_tripmine.mdl",
	"models/w_357.mdl",
	"models/w_357ammobox.mdl",
	"models/w_9mmar.mdl",
	"models/w_9mmarclip.mdl",
	"models/w_9mmclip.mdl",
	"models/w_9mmhandgun.mdl",
	"models/w_antidote.mdl",
	"models/w_argrenade.mdl",
	"models/w_battery.mdl",
	"models/w_chainammo.mdl",
	"models/w_crossbow.mdl",
	"models/w_crossbow_clip.mdl",
	"models/w_crowbar.mdl",
	"models/w_egon.mdl",
	"models/w_gauss.mdl",
	"models/w_gaussammo.mdl",
	"models/w_grenade.mdl",
	"models/w_hgun.mdl",
	"models/w_longjump.mdl",
	"models/w_medkit.mdl",
	"models/w_oxygen.mdl",
	"models/w_rpg.mdl",
	"models/w_rpgammo.mdl",
	"models/w_satchel.mdl",
	"models/w_security.mdl",
	"models/w_shotbox.mdl",
	"models/w_shotgun.mdl",
	"models/w_sqknest.mdl",
	"models/w_squeak.mdl",
	"models/w_suit.mdl",
	"models/w_weaponbox.mdl",
)

SMOKE_CASE_ALIASES = (
	("models/p_9mmar.mdl", "models/p_9mmAR.mdl"),
	("models/v_9mmar.mdl", "models/v_9mmAR.mdl"),
	("models/w_9mmar.mdl", "models/w_9mmAR.mdl"),
	("models/w_9mmarclip.mdl", "models/w_9mmARclip.mdl"),
	("models/w_argrenade.mdl", "models/w_ARgrenade.mdl"),
	("models/p_9mmAR.mdl", "models/p_9mmar.mdl"),
	("models/v_9mmAR.mdl", "models/v_9mmar.mdl"),
	("models/w_9mmAR.mdl", "models/w_9mmar.mdl"),
	("models/w_9mmARclip.mdl", "models/w_9mmarclip.mdl"),
	("models/w_ARgrenade.mdl", "models/w_argrenade.mdl"),
)


def copy_if_present(source_root: Path, output_root: Path, relative: str) -> None:
	source = source_root / relative
	if not source.is_file():
		return

	destination = output_root / relative
	destination.parent.mkdir(parents=True, exist_ok=True)
	shutil.copy2(source, destination)


def copy_tree_if_present(source_root: Path, output_root: Path, relative: str) -> None:
	source = source_root / relative
	if not source.is_dir():
		return

	for child in sorted(source.rglob("*")):
		if child.is_file():
			destination = output_root / child.relative_to(source_root)
			destination.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(child, destination)


def copy_alias_if_present(output_root: Path, source_relative: str, alias_relative: str) -> None:
	source = output_root / source_relative
	if not source.is_file():
		return

	destination = output_root / alias_relative
	destination.parent.mkdir(parents=True, exist_ok=True)
	if not destination.exists():
		shutil.copy2(source, destination)


def in_strict_case_dir(rel_root: str) -> bool:
	if not rel_root or rel_root == ".":
		return False
	return any(rel_root == strict or rel_root.startswith(f"{strict}/") for strict in STRICT_CASE_DIRS)


def staged_retail_relative(rel_root: str, filename: str) -> str:
	rel_path = os.path.join(rel_root, filename).replace("\\", "/")
	if rel_path.startswith("./"):
		rel_path = rel_path[2:]
	if in_strict_case_dir(rel_root):
		return rel_path.lower()
	return rel_path


def should_skip_retail_asset(filename: str) -> bool:
	_, ext = os.path.splitext(filename)
	return ext.lower() in UNSUPPORTED_EXTENSIONS


def _gc_menu_parse_layout(source: Path, layout: Path) -> tuple[int, int, list[tuple[Path, int, int]]]:
	tiles: list[tuple[Path, int, int]] = []
	bg_w, bg_h = 800, 600
	for line in layout.read_text(encoding="utf-8", errors="replace").splitlines():
		parts = line.split()
		if not parts:
			continue
		if parts[0] == "resolution" and len(parts) >= 3:
			bg_w, bg_h = int(parts[1]), int(parts[2])
			continue
		if len(parts) < 4 or not parts[0].startswith("resource/background/"):
			continue
		# Layout columns are: path, fit/scaled, x, y (x/y are the last two ints).
		try:
			x = int(parts[-2])
			y = int(parts[-1])
		except ValueError:
			continue
		tile_path = source / parts[0]
		if not tile_path.is_file():
			continue
		tiles.append((tile_path, x, y))
	return bg_w, bg_h, tiles


def _gc_menu_nonblack_ratio(image) -> float:
	# Sample a downscaled copy so HD layouts stay cheap to score.
	sample = image.resize((80, 40))
	pixels = sample.load()
	width, height = sample.size
	total = width * height
	if total <= 0:
		return 0.0
	nonblack = 0
	for y in range(height):
		for x in range(width):
			pixel = pixels[x, y]
			if pixel[0] > 12 or pixel[1] > 12 or pixel[2] > 12:
				nonblack += 1
	return nonblack / float(total)


def _gc_menu_preserve_retail_tone(image):
	"""Lift crushed HD steam tiles so the lambda/lab backdrop reads on TV.

	HD_BackgroundLayout composites sit near mean≈15 / p95≈30 (near-black on
	a 320×240 soft HUD). Probe 20261104-235523 showed 4 dark quadrants with
	no readable backdrop. Stretch midtones toward a retail-visible p95 while
	keeping the moody industrial look.
	"""
	from PIL import Image
	from PIL import ImageEnhance, ImageOps
	import numpy as np

	rgba = image.convert("RGBA")
	rgb = rgba.convert("RGB")
	arr = np.asarray(rgb, dtype=np.float32)
	p95 = float(np.percentile(arr, 95))
	# Target a readable soft-HUD backdrop without clipping the lambda emblem.
	target_p95 = 168.0
	if p95 > 1.0 and p95 < target_p95:
		arr = np.clip(arr * (target_p95 / p95), 0, 255)
	rgb = Image.fromarray(arr.astype(np.uint8), mode="RGB")
	rgb = ImageEnhance.Contrast(rgb).enhance(1.12)
	# Mild autocontrast keeps crushed blacks from swallowing the whole sheet.
	rgb = ImageOps.autocontrast(rgb, cutoff=1)
	return rgb.convert("RGBA")


def _gc_menu_fit_aspect(image, size):
	"""Crop wide retail layouts before fitting the fixed 4:3 GC menu canvas."""
	target_w, target_h = size
	src_w, src_h = image.size
	if not src_w or not src_h or not target_w or not target_h:
		return image.resize(size)
	src_aspect = src_w / float(src_h)
	target_aspect = target_w / float(target_h)
	if src_aspect > target_aspect:
		crop_w = max(1, int(round(src_h * target_aspect)))
		left = (src_w - crop_w) // 2
		image = image.crop((left, 0, left + crop_w, src_h))
	elif src_aspect < target_aspect:
		crop_h = max(1, int(round(src_w / target_aspect)))
		top = (src_h - crop_h) // 2
		image = image.crop((0, top, src_w, top + crop_h))
	return image.resize(size)


def _gc_menu_synthetic_background(size: tuple[int, int]):
	"""Fallback when retail tiles are missing or pure black placeholders."""
	from PIL import Image

	width, height = size
	image = Image.new("RGBA", size)
	pixels = image.load()
	for y in range(height):
		for x in range(width):
			# Vertical cool gradient with a faint warm highlight on the left,
			# similar to the retail menu's industrial backdrop.
			t = y / max(1, height - 1)
			warm = max(0, 48 - abs(x - width * 0.28) * 0.35)
			pixels[x, y] = (
				int(18 + warm + t * 10),
				int(24 + warm * 0.5 + t * 14),
				int(40 + t * 36),
				255,
			)
	return image


def _gc_menu_font(source: Path, image_font, size: int, medium: bool = False):
	"""Use the shipped UI font so baked text is reproducible on every host."""
	font_name = "FiraSans-Medium.ttf" if medium else "FiraSans-Regular.ttf"
	candidates = (
		source.parent / "platform" / "resource" / "linux_fonts" / font_name,
		Path(__file__).resolve().parents[1] / "fonts" / "GameCube.ttf",
	)
	for path in candidates:
		if path.is_file():
			return image_font.truetype(str(path), size)
	return image_font.load_default()


GC_MENU_BANNER_SIZE = (640, 104)
GC_MENU_BANNER_FRAME_SEC = 2.0


def _stage_gc_menu_logo_banner(source: Path, menu_dir: Path) -> bool:
	"""resource/gc_menu/logo_banner.{cmp,tga} from a frame of media/logo.avi."""
	avi = source / "media" / "logo.avi"
	if not avi.is_file() or not shutil.which("ffmpeg"):
		return False
	try:
		import numpy as np
		from PIL import Image
	except ImportError:
		return False
	with tempfile.TemporaryDirectory(prefix="xash3d-gc-banner-") as tmp:
		frame = Path(tmp) / "frame.png"
		result = subprocess.run(
			["ffmpeg", "-loglevel", "error", "-y", "-ss", str(GC_MENU_BANNER_FRAME_SEC),
			 "-i", str(avi), "-frames:v", "1", str(frame)],
			capture_output=True)
		if result.returncode != 0 or not frame.is_file():
			print("Warning: logo.avi frame extract failed; menu keeps the static logo.", file=sys.stderr)
			return False
		banner = Image.open(frame).convert("RGB").resize(GC_MENU_BANNER_SIZE, Image.Resampling.LANCZOS)
	w, h = GC_MENU_BANNER_SIZE
	header = b"GCMP" + struct.pack(">HH", w, h) + bytes(24)
	(menu_dir / "logo_banner.cmp").write_bytes(header + gx_cmpr_encode(np.asarray(banner)))
	banner.resize((w // 4, h // 4), Image.Resampling.LANCZOS).save(menu_dir / "logo_banner.tga", format="TGA")
	print("GameCube menu banner: logo.avi frame baked as logo_banner.cmp 640x104")
	return True


def stage_gc_menu_assets(source: Path, output: Path) -> bool:
	"""Bake a single MEM1-friendly retail menu background and title logo."""
	try:
		from PIL import Image, ImageDraw, ImageFont
	except ImportError:
		print("Warning: Pillow not installed; skipping GameCube menu background bake.", file=sys.stderr)
		return False

	# Preserve host-prebaked Lost Coast title plates (Source console/background01).
	# Without this, lean CapFaces stages fall through to the synthetic HL1 sheet.
	prebaked_menu = source / "resource" / "gc_menu"
	prebaked_src = prebaked_menu / "SOURCE.txt"
	if (
		prebaked_src.is_file()
		and "Lost Coast" in prebaked_src.read_text(encoding="utf-8", errors="replace")
		and (prebaked_menu / "background.tga").is_file()
		and (prebaked_menu / "bg_0_0.tga").is_file()
	):
		menu_dir = output / "resource" / "gc_menu"
		menu_dir.mkdir(parents=True, exist_ok=True)
		for child in sorted(prebaked_menu.iterdir()):
			if child.is_file():
				shutil.copy2(child, menu_dir / child.name)
		_stage_gc_menu_btn_strips(source, menu_dir)
		print(
			f"GameCube menu assets: preserve Lost Coast retail title "
			f"{(menu_dir / 'background.tga').relative_to(output)}"
		)
		return True

	candidates = [
		source / "resource" / "BackgroundLayout.txt",
		source / "resource" / "HD_BackgroundLayout.txt",
	]
	best = None
	best_ratio = -1.0
	best_label = ""

	for layout in candidates:
		if not layout.is_file():
			continue
		bg_w, bg_h, tiles = _gc_menu_parse_layout(source, layout)
		if not tiles:
			continue
		canvas = Image.new("RGBA", (bg_w, bg_h), (24, 28, 40, 255))
		for tile_path, x, y in tiles:
			tile = Image.open(tile_path).convert("RGBA")
			canvas.paste(tile, (x, y))
		ratio = _gc_menu_nonblack_ratio(canvas)
		if ratio > best_ratio:
			best = canvas
			best_ratio = ratio
			best_label = f"{layout.name} ({len(tiles)} tiles, nonblack={ratio:.1%})"

	menu_dir = output / "resource" / "gc_menu"
	menu_dir.mkdir(parents=True, exist_ok=True)

	# Soft HUD is 320×240 (2× to the 640×480 EFB). Bake the menu sheet at
	# that logical size so each quadrant is 160×120 (~56 KiB RGB) and can
	# LoadFile+decode after the intro tip — 320×240 tiles (~230 KiB files)
	# soft-failed FS_LoadFile before ImageLib ever ran.
	gc_menu_bg_size = (320, 240)
	bake_src_size = (640, 480)
	if best is not None and best_ratio >= 0.02:
		# Retail tone: the HD steam tiles as drawn by the PC menu (mean ~15,
		# p95 ~31, the same as the retail screenshot).  The old lift to p95 168
		# made up for an unreadable 320x240 soft HUD and washed the sheet out
		# on the full-resolution GX menu.
		hires = _gc_menu_fit_aspect(best, bake_src_size).convert("RGBA")
		background = hires.resize(gc_menu_bg_size, Image.Resampling.LANCZOS)
		source_note = best_label
	else:
		background = _gc_menu_synthetic_background(gc_menu_bg_size)
		source_note = "synthetic fallback (retail tiles missing or pure black)"

	# Four 160×120 RGB tiles (~56 KiB decode each). Keep a full soft-res sheet
	# for tools/hosts; live menu uses the layout tiles.
	background_rgb = background.convert("RGB")
	background_path = menu_dir / "background.tga"
	background_rgb.save(background_path, format="TGA")
	background_rgb.save(menu_dir / "background.bmp", format="BMP")
	tile_w, tile_h = 160, 120
	layout_lines = [f"resolution\t{gc_menu_bg_size[0]}\t{gc_menu_bg_size[1]}", ""]
	for row, y in enumerate((0, tile_h)):
		for col, x in enumerate((0, tile_w)):
			tile = background_rgb.crop((x, y, x + tile_w, y + tile_h))
			tile_name = f"bg_{row}_{col}.tga"
			tile.save(menu_dir / tile_name, format="TGA")
			layout_lines.append(
				f"resource/gc_menu/{tile_name}\tfit\t{x}\t{y}"
			)
	(menu_dir / "BackgroundLayout.txt").write_text("\n".join(layout_lines) + "\n")

	# Full-resolution quadrants for the GX menu: the same layout tile names as
	# 320x240 GX CMPR (bg_<r>_<c>.cmp, 32-byte "GCMP" header with big-endian
	# width/height).  The renderer binds these instead of stretching the
	# 160x120 TGAs; 4 bpp keeps the 640x480 sheet at the same ~150 KiB.
	try:
		import numpy as np
		full = (hires if best is not None and best_ratio >= 0.02
			else _gc_menu_synthetic_background(bake_src_size)).convert("RGB")
		if full.size != bake_src_size:
			full = full.resize(bake_src_size, Image.Resampling.LANCZOS)
		cw, ch = bake_src_size[0] // 2, bake_src_size[1] // 2
		for row, y in enumerate((0, ch)):
			for col, x in enumerate((0, cw)):
				quad = np.asarray(full.crop((x, y, x + cw, y + ch)))
				header = b"GCMP" + struct.pack(">HH", cw, ch) + bytes(24)
				(menu_dir / f"bg_{row}_{col}.cmp").write_bytes(header + gx_cmpr_encode(quad))
	except ImportError:
		print("Warning: numpy missing; menu background stays at 320x240.", file=sys.stderr)

	# Prefer the original white retail title logo so the baked menu matches the
	# real menu composition instead of the earlier orange fallback treatment.
	logo = None
	logo_src = source / "resource" / "logo.tga"
	if logo_src.is_file():
		logo = Image.open(logo_src).convert("RGBA")
	else:
		logo_src = source / "resource" / "logo_game.tga"
		if logo_src.is_file():
			logo = Image.open(logo_src).convert("RGBA")
	if logo is not None:
		# Match on-screen titleLogo size (512×56 menu units) at source density.
		logo.thumbnail((512, 64), Image.Resampling.LANCZOS)
		logo_pixels = logo.load()
		# Opaque RGB wordmarks (DoD logo_game) use black as the matte — punch
		# near-black through to alpha so the splash shows behind the letters.
		opaque_rgb = []
		for py in range(logo.height):
			for px in range(logo.width):
				r, g, b, a = logo_pixels[px, py]
				if a >= 250 and (r + g + b) < 24:
					logo_pixels[px, py] = (r, g, b, 0)
					continue
				if a:
					opaque_rgb.append((r, g, b))
		# Amber remap is for white HL wordmarks only. Coloured logos (DoD olive
		# stencil) must keep retail palette.
		if opaque_rgb:
			mean_luma = sum((r + g + b) / 3 for r, g, b in opaque_rgb) / len(opaque_rgb)
			if mean_luma >= 180:
				for py in range(logo.height):
					for px in range(logo.width):
						r, g, b, a = logo_pixels[px, py]
						if a:
							logo_pixels[px, py] = (214, 138, 26, a)
			elif (source / "liblist.gam").is_file() and "Day of Defeat" in (
				(source / "liblist.gam").read_text(encoding="latin1", errors="replace")
			):
				# Retail DoD's lower-left title mark is warm brass, not the olive
				# green asset tint. Preserve its thin outline and alpha coverage.
				for py in range(logo.height):
					for px in range(logo.width):
						r, g, b, a = logo_pixels[px, py]
						if a:
							logo_pixels[px, py] = (192, 151, 58, a)
		logo.save(menu_dir / "logo.tga", format="TGA")

	# WON title banner: retail media/logo.avi is a 640x100 strip (white
	# HALF-LIFE over its own animated band) drawn full width at y=70 of the
	# 640x480 menu.  Bake one frame as GX CMPR (640x104, CMPR needs multiples
	# of 8) for the GX menu, plus a small TGA so mainui can load the picture.
	_stage_gc_menu_logo_banner(source, menu_dir)

	# Optional low-memory 3×3 preview tiles for probes that cannot hold the
	# full 640×480 sheet; the live menu uses background.tga + logo.tga.
	composite = background.convert("RGBA")
	if logo is not None:
		logo_draw = logo.resize(
			(int(640 * 0.74), max(1, int(logo.height * (640 * 0.74 / max(logo.width, 1))))),
			Image.Resampling.LANCZOS)
		composite.alpha_composite(logo_draw, (int((640 - logo_draw.width) / 2), int(480 * 0.08)))
	composite = composite.resize((96, 96), Image.Resampling.LANCZOS)
	for row in range(3):
		for col in range(3):
			tile = composite.crop((col * 32, row * 32, (col + 1) * 32, (row + 1) * 32))
			tile.save(menu_dir / f"menu_{row * 3 + col}.tga", format="TGA")

	# Slim retail picbutton strips — full btns_main.bmp (~865 KiB) tip-hangs
	# after intro. Extract only the main-menu rows into ~36 KiB RGB TGAs.
	_stage_gc_menu_btn_strips(source, menu_dir)

	print(
		f"GameCube menu assets: baked {background_path.relative_to(output)} "
		f"{gc_menu_bg_size[0]}x{gc_menu_bg_size[1]} from {source_note}"
	)
	return True


def _stage_gc_menu_btn_strips(source: Path, menu_dir: Path) -> None:
	"""Bake WON/Steam btns_main rows used by the GameCube main menu."""
	try:
		from PIL import Image
	except ImportError:
		return

	candidates = [
		source / "gfx" / "shell" / "btns_main.bmp",
		Path(__file__).resolve().parents[1]
		/ "3rdparty"
		/ "extras"
		/ "xash-extras"
		/ "gfx"
		/ "shell"
		/ "btns_main.bmp",
	]
	btns_path = next((p for p in candidates if p.is_file()), None)
	if btns_path is None:
		print("Warning: btns_main.bmp not found; GC menu keeps text buttons.", file=sys.stderr)
		return

	# Convert off indexed BMP first — paste into a fresh P image drops the
	# palette and baked solid-black strips (probe 20261104-234653: status
	# text only, no New Game/Hazard/Quit art). Pillow already tops BMP rows.
	im = Image.open(btns_path).convert("RGB")
	w, h = im.size
	btn_h = 26
	cell = btn_h * 3
	# EDefaultBtns indices → output stem (resource/gc_menu/btn_<stem>.tga)
	wanted = {
		0: "new_game",
		1: "resume_game",
		2: "hazard_course",
		3: "config",
		4: "load_game",
		5: "save_load_game",
		7: "quit",
		9: "easy",
		10: "medium",
		11: "hard",
		14: "cancel",
		56: "disconnect",
	}

	def extract_btn(btn_id: int) -> "Image.Image":
		states = []
		for state in range(3):
			st = Image.new("RGB", (w, btn_h))
			for line in range(btn_h):
				src_row = btn_id * cell + state * btn_h + line
				st.paste(im.crop((0, src_row, w, src_row + 1)), (0, line))
			states.append(st)
		out = Image.new("RGB", (w, cell))
		for i, st in enumerate(states):
			out.paste(st, (0, i * btn_h))
		return out

	def _thicken_orange(strip: "Image.Image") -> "Image.Image":
		"""1px dilate of non-black ink so soft/YUV chroma keeps glyphs readable."""
		px = strip.load()
		w0, h0 = strip.size
		ink = [[False] * w0 for _ in range(h0)]
		for y in range(h0):
			for x in range(w0):
				r, g, b = px[x, y]
				ink[y][x] = (r > 24) or (g > 24) or (b > 16)
		out = strip.copy()
		opx = out.load()
		for y in range(h0):
			for x in range(w0):
				if ink[y][x]:
					continue
				# Neighbor ink → copy brightest neighbor (keeps orange, fills holes).
				best = None
				best_luma = -1
				for dy in (-1, 0, 1):
					for dx in (-1, 0, 1):
						nx, ny = x + dx, y + dy
						if nx < 0 or ny < 0 or nx >= w0 or ny >= h0 or not ink[ny][nx]:
							continue
						r, g, b = px[nx, ny]
						luma = (r * 3 + g * 6 + b) // 10
						if luma > best_luma:
							best_luma = luma
							best = (r, g, b)
				if best is not None:
					opx[x, y] = best
		return out

	written = 0
	for btn_id, stem in wanted.items():
		if (btn_id + 1) * cell > h:
			continue
		strip = _thicken_orange(extract_btn(btn_id))
		strip.save(menu_dir / f"btn_{stem}.tga", format="TGA")
		written += 1
	print(f"GameCube menu buttons: baked {written} retail strips from {btns_path.name}")

	# Slim New Game banner (extras head_newgame.tga is 460×80 — fine for MEM1).
	banner_candidates = [
		source / "gfx" / "shell" / "head_newgame.tga",
		Path(__file__).resolve().parents[1]
		/ "3rdparty"
		/ "extras"
		/ "xash-extras"
		/ "gfx"
		/ "shell"
		/ "head_newgame.tga",
	]
	banner_src = next((p for p in banner_candidates if p.is_file()), None)
	if banner_src is not None:
		# Convert off indexed/P first — palette paste onto RGB drops the
		# palette and baked a black banner (same trap as btn strips).
		banner = Image.open(banner_src).convert("RGBA")
		banner_rgb = Image.new("RGB", banner.size, (0, 0, 0))
		banner_rgb.paste(banner, (0, 0), banner)
		banner_rgb.save(menu_dir / "head_newgame.tga", format="TGA")
		print(f"GameCube menu banner: baked head_newgame.tga {banner_rgb.size[0]}x{banner_rgb.size[1]}")


def stage_gc_loading_assets(source: Path, output: Path) -> bool:
	"""Bake MEM1-friendly HL1 loading plaque: bald scientist + lambda motif.

	Used by GC_DrawLoadingStatus (G60) so map load shows retail-themed art and
	a progress bar instead of a flat debug color field.
	"""
	try:
		from PIL import Image, ImageDraw, ImageEnhance, ImageFilter
	except ImportError:
		print("Warning: Pillow not installed; skipping GameCube loading plaque bake.", file=sys.stderr)
		return False

	menu_dir = output / "resource" / "gc_menu"
	menu_dir.mkdir(parents=True, exist_ok=True)

	# Lost Coast CapFaces title: keep console/background01 loading+intro plates.
	src_note = menu_dir / "SOURCE.txt"
	if (
		src_note.is_file()
		and "Lost Coast" in src_note.read_text(encoding="utf-8", errors="replace")
		and (menu_dir / "loading.tga").is_file()
		and (menu_dir / "background.tga").is_file()
	):
		# Ensure intro matches the title plate when the bake omitted a dedicated intro.
		if not (menu_dir / "intro.tga").is_file():
			shutil.copy2(menu_dir / "background.tga", menu_dir / "intro.tga")
		if not (menu_dir / "loading.tga").is_file():
			shutil.copy2(menu_dir / "background.tga", menu_dir / "loading.tga")
		print(
			f"GameCube loading assets: preserve Lost Coast retail title "
			f"{(menu_dir / 'loading.tga').relative_to(output)}"
		)
		return True

	# Distressed lambda wallpaper from HD loading tiles when present.
	bg = Image.new("RGB", (320, 240), (18, 16, 14))
	layout = source / "resource" / "HD_BackgroundLoadingLayout.txt"
	if layout.is_file():
		bg_w, bg_h, tiles = _gc_menu_parse_layout(source, layout)
		if tiles:
			canvas = Image.new("RGB", (bg_w, bg_h), (0, 0, 0))
			for tile_path, x, y in tiles:
				try:
					canvas.paste(Image.open(tile_path).convert("RGB"), (x, y))
				except OSError:
					continue
			boosted = ImageEnhance.Contrast(canvas).enhance(3.2)
			boosted = ImageEnhance.Brightness(boosted).enhance(3.8)
			bg = boosted.resize((320, 240), Image.Resampling.LANCZOS)
			bg = ImageEnhance.Color(bg).enhance(0.35)

	# Soft vignette + dark lower band reserved for status/progress UI.
	vignette = Image.new("L", (320, 240), 0)
	vdraw = ImageDraw.Draw(vignette)
	vdraw.ellipse((-40, -60, 360, 280), fill=220)
	vignette = vignette.filter(ImageFilter.GaussianBlur(28))
	bg = Image.composite(bg, Image.new("RGB", (320, 240), (8, 8, 8)), vignette)
	band = Image.new("RGBA", (320, 240), (0, 0, 0, 0))
	ImageDraw.Draw(band).rectangle((0, 168, 320, 240), fill=(10, 10, 12, 210))
	bg = Image.alpha_composite(bg.convert("RGBA"), band).convert("RGB")

	# Bald Black Mesa scientist (Walter) — the iconic HL1 "bald guy".
	scientist = source / "models" / "player" / "scientist" / "Scientist.bmp"
	if not scientist.is_file():
		scientist = source / "models" / "player" / "scientist" / "scientist.bmp"
	if scientist.is_file():
		char = Image.open(scientist).convert("RGBA")
		# Drop near-black backdrop so he composites cleanly.
		datas = char.getdata()
		cleaned = []
		for r, g, b, a in datas:
			if r < 12 and g < 12 and b < 12:
				cleaned.append((0, 0, 0, 0))
			else:
				cleaned.append((r, g, b, 255))
		char.putdata(cleaned)
		char = char.resize((118, 144), Image.Resampling.NEAREST)
		bg_rgba = bg.convert("RGBA")
		bg_rgba.paste(char, (18, 28), char)
		bg = bg_rgba.convert("RGB")

	# Lambda watermark from retail gfx when available.
	lam = source / "gfx" / "lambda.bmp"
	if lam.is_file():
		mark = Image.open(lam).convert("RGBA").resize((48, 48), Image.Resampling.NEAREST)
		mark.putalpha(mark.split()[0].point(lambda v: 90 if v > 8 else 0))
		layered = bg.convert("RGBA")
		layered.paste(mark, (250, 28), mark)
		bg = layered.convert("RGB")

	loading_path = menu_dir / "loading.tga"
	bg.save(loading_path, format="TGA")

	# Intro still: bald scientist plaque used for stage demos / boot hold.
	intro = Image.new("RGB", (320, 240), (0, 0, 0))
	if scientist.is_file():
		char = Image.open(scientist).convert("RGBA")
		datas = char.getdata()
		cleaned = []
		for r, g, b, a in datas:
			if r < 12 and g < 12 and b < 12:
				cleaned.append((0, 0, 0, 0))
			else:
				cleaned.append((r, g, b, 255))
		char.putdata(cleaned)
		char = char.resize((150, 184), Image.Resampling.NEAREST)
		frame = Image.new("RGBA", (320, 240), (0, 0, 0, 255))
		# Sepia-ish HL portrait plate.
		plate = Image.new("RGB", (200, 200), (42, 32, 22))
		plate_draw = ImageDraw.Draw(plate)
		plate_draw.rectangle((2, 2, 197, 197), outline=(160, 110, 40), width=2)
		frame.paste(plate, (60, 20))
		frame.paste(char, (85, 28), char)
		# Title strip under the plate.
		td = ImageDraw.Draw(frame)
		td.rectangle((60, 222, 260, 236), fill=(20, 16, 12))
		td.text((96, 224), "HALF-LIFE", fill=(230, 170, 50))
		intro = frame.convert("RGB")
	intro_path = menu_dir / "intro.tga"
	intro.save(intro_path, format="TGA")

	print(
		f"GameCube loading assets: baked {loading_path.relative_to(output)} "
		f"and {intro_path.relative_to(output)} (bald scientist + HL motif)"
	)
	return True


# Small allowlist injected into gamecube-bootstrap.pk3 as gc_studio/*.mdl so
# New Game can load real meshes without ISO9660 lookups in huge models/, and
# without mounting an extra ZIP (which tips the New Game MEM1 cliff).
GC_STUDIO_MODELS = (
	"models/v_crowbar.mdl",
	"models/v_9mmhandgun.mdl",
	# DoD CapFaces team weapons (lean-stripped on inject; real names on disc).
	"models/v_garand.mdl",
	"models/v_mp40.mdl",
	"models/v_98k.mdl",
	"models/v_tommy.mdl",
	"models/v_colt.mdl",
	"models/v_luger.mdl",
	"models/w_crowbar.mdl",
	# DoD CapFaces map props (env_model / dod_object).
	# Tip: skip forts/kingtiger; caen unique order saturates 6 slots before bulb.
	"models/mapmodels/hk_satchel.mdl",
	"models/mapmodels/horse_statue.mdl",
	"models/mapmodels/by_rubble_set.mdl",
	"models/mapmodels/sandbags.mdl",
	"models/mapmodels/by_canopy.mdl",
	"models/mapmodels/arc_xer_tree2.mdl",
	"models/mapmodels/furniture.mdl",
	"models/mapmodels/bulb.mdl",
	# dod_avalanche env_models — full-skin props (ava-prop-1).
	"models/mapmodels/sherman.mdl",
	"models/mapmodels/jagd.mdl",
	"models/mapmodels/by_banister.mdl",
	"models/mapmodels/flags.mdl",
	# Small world NPC — gman (~76KB) OOMs libc malloc after crowbars on GC.
	"models/roach.mdl",
	# G315/G371: deferred GX entity pass promotes these NPC meshes.
	"models/headcrab.mdl",
	"models/zombie.mdl",
	"models/scientist.mdl",
	"models/barney.mdl",
	"models/houndeye.mdl",
	# c0a0's forklift: the plain heap read fails after the BSP (47 KiB).
	"models/forklift.mdl",
	"models/loader.mdl",
	# c1a1f monster_furniture uses filecabinet (there is no furniture.mdl).
	"models/filecabinet.mdl",
	# c0a0d: the G-Man, skins in gmant.mdl.
	"models/gman.mdl",
	"models/gmant.mdl",
	# G376: external GoldSrc skins (*t.mdl) for Flipper NPC texturing.
	"models/scientistt.mdl",
	"models/barneyt.mdl",
	"models/headcrabt.mdl",
	"models/zombiet.mdl",
	"models/houndeyet.mdl",
	# Xen boss and reinforcements. The boss has embedded skins; the others
	# use separate T.mdl atlases and must keep both files accessible.
	"models/nihilanth.mdl",
	"models/big_mom.mdl",
	"models/big_momt.mdl",
	"models/baby_headcrab.mdl",
	"models/controller.mdl",
	"models/controllert.mdl",
	"models/islave.mdl",
	"models/islavet.mdl",
)

# Direct-load HUD sheets for lean New Game Redraw (ISO9660 sprites/ lookups
# false-miss like models/). Keep the set tiny — bootstrap MEM is tight.
GC_HUD_SPRITES = (
	# G288: lean single-frame aliases — fat retail soft-fails under tip.
	("sprites/320_pain.spr", "sprites/gc_320_pain.spr"),
	("sprites/320_train.spr", "sprites/gc_320_train.spr"),
	("sprites/320hud2.spr", "sprites/gc_320hud2.spr"),
	# G173: lean 64×64 downsample of fat 320hud1 (~66 KiB → ~5 KiB).
	("sprites/320hud1.spr", "sprites/gc_320hud1.spr"),
	# G174: lean 64×64 downsample of 128×128 crosshairs (~17 KiB → ~5 KiB).
	("sprites/crosshairs.spr", "sprites/gc_crosshairs.spr"),
	# G320: reactor electricity / env_beam (~7 KiB retail; tip-safe as-is).
	"sprites/lgtning.spr",
)

# Lean skybox BMPs for New Game RGB565 fills. Use gc_desert* names so retail
# ISO9660 gfx/env/desert*.bmp cannot shadow a failed decode over the ZIP copy.
GC_SKY_SIDES = (
	("gfx/env/desertup.bmp", "gfx/env/gc_desertup.bmp"),
	("gfx/env/desertft.bmp", "gfx/env/gc_desertft.bmp"),
	("gfx/env/desertbk.bmp", "gfx/env/gc_desertbk.bmp"),
	("gfx/env/desertrt.bmp", "gfx/env/gc_desertrt.bmp"),
)

# G293: tip-safe lean decal TGAs. Path must NOT be under decals/ — that
# collides with decals.wad on ISO9660 and hung New Game before map load.
GC_LEAN_DECALS = (
	("{shot1", "gc_decals/gc_shot1.tga"),
	("{smscorch1", "gc_decals/gc_smscorch1.tga"),
)


def lean_decal_tga_from_wad(wad_path: Path, lump_name: str) -> bytes | None:
	"""Decode one WAD3 MIP lump to an uncompressed 32-bit TGA (tip-safe size)."""
	if not wad_path.is_file():
		return None
	try:
		data = wad_path.read_bytes()
	except OSError:
		return None
	if len(data) < 12:
		return None
	magic, count, doff = struct.unpack_from("<4sii", data, 0)
	if magic not in (b"WAD2", b"WAD3") or count < 1:
		return None
	lump = None
	for i in range(count):
		off = doff + i * 32
		if off + 32 > len(data):
			break
		filepos, disksize, _size, _typ, compression, _pad, name = struct.unpack_from(
			"<iiibbh16s", data, off
		)
		n = name.split(b"\0", 1)[0].decode("latin-1", "replace")
		if n.lower() != lump_name.lower() or compression:
			continue
		if filepos < 0 or disksize < 40 or filepos + disksize > len(data):
			return None
		lump = data[filepos : filepos + disksize]
		break
	if not lump:
		return None
	w, h = struct.unpack_from("<II", lump, 16)
	offs = struct.unpack_from("<IIII", lump, 24)
	if w < 1 or h < 1 or w > 64 or h > 64:
		return None
	if offs[0] + w * h > len(lump):
		return None
	pix = lump[offs[0] : offs[0] + w * h]
	last = offs[3] + max(1, w // 8) * max(1, h // 8)
	rest = lump[last:] if last < len(lump) else b""
	if len(rest) >= 2 + 768:
		pal = rest[2 : 2 + 768]
	elif len(lump) >= 768:
		pal = lump[-768:]
	else:
		return None
	rgba = bytearray(w * h * 4)
	for i, idx in enumerate(pix):
		r, g, b = pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2]
		a = 0 if idx == 255 else 255
		rgba[i * 4 : i * 4 + 4] = bytes((r, g, b, a))
	header = bytearray(18)
	header[2] = 2
	header[12] = w & 255
	header[13] = (w >> 8) & 255
	header[14] = h & 255
	header[15] = (h >> 8) & 255
	header[16] = 32
	header[17] = 8
	rows = []
	for y in range(h - 1, -1, -1):
		row = bytearray()
		for x in range(w):
			i = (y * w + x) * 4
			r, g, b, a = rgba[i : i + 4]
			row += bytes((b, g, r, a))
		rows.append(row)
	return bytes(header) + b"".join(rows)


def inject_gc_decals_into_bootstrap(archive: "zipfile.ZipFile", data: Path) -> int:
	"""G293: inject lean shot/scorch TGAs so Flipper decals skip fat WAD alloc."""
	wad = data / "decals.wad"
	staged = 0
	for lump_name, arc_rel in GC_LEAN_DECALS:
		arcname = arc_rel.lower()
		if arcname in archive.NameToInfo:
			continue
		payload = lean_decal_tga_from_wad(wad, lump_name)
		if not payload:
			continue
		archive.writestr(arcname, payload, compress_type=zipfile.ZIP_STORED)
		print(f"  lean decal {lump_name}: → {arcname} ({len(payload)} bytes)")
		staged += 1
	return staged


def _downsample_bmp_to_bytes(src: Path, size: int = 64) -> bytes | None:
	"""Return a size×size 8-bit BMP suitable for ImageLib sky loads."""
	try:
		from PIL import Image
	except ImportError:
		return None
	if not src.is_file():
		return None
	with Image.open(src) as img:
		img = img.convert("P")
		img = img.resize((size, size), Image.Resampling.NEAREST)
		import io

		buf = io.BytesIO()
		img.save(buf, format="BMP")
		return buf.getvalue()


def inject_gc_sky_into_bootstrap(archive: "zipfile.ZipFile", data: Path) -> int:
	"""Add lean 16×16 desert sky BMPs into bootstrap under gc_desert* names.

	G300: 64→16 so ImageLib decode is ≤1 KiB (32² soft-failed under tip).
	Also mirror media/sky_*.cmp (GX CMPR Flipper skies) into the bootstrap
	pk3 — ISO9660 directory find misses these long names on some grafts.
	"""
	staged = 0
	for src_rel, arc_rel in GC_SKY_SIDES:
		src = data / src_rel
		payload = _downsample_bmp_to_bytes(src, 16)
		if not payload:
			continue
		arcname = arc_rel.lower()
		if arcname in archive.NameToInfo:
			continue
		archive.writestr(arcname, payload, compress_type=zipfile.ZIP_STORED)
		staged += 1
	media = data / "media"
	if media.is_dir():
		for cmp in sorted(media.glob("sky_*.cmp")):
			if cmp.stat().st_size <= 0:
				continue
			arcname = f"media/{cmp.name}".lower()
			if arcname in archive.NameToInfo:
				continue
			archive.write(cmp, arcname, compress_type=zipfile.ZIP_STORED)
			staged += 1
		# CapFaces primary packs only in bootstrap (ISO9660 long-name miss).
		# Spatial cells ship as cf_<map>_spatial.pk3 at gamedir root.
		for gcp in sorted(media.glob("cf_*.gcp")):
			name = gcp.name.lower()
			if "_s" in name[3:-4]:
				continue
			if gcp.stat().st_size <= 0 or gcp.stat().st_size > 2 * 1024 * 1024:
				continue
			arcname = f"media/{name}"
			if arcname in archive.NameToInfo:
				continue
			archive.write(gcp, arcname, compress_type=zipfile.ZIP_STORED)
			staged += 1
		# World DL packs: must be ZIP_STORED (DEFLATE → inflateInit2 OOM under
		# tip). Keep under 4 MiB; chunked STORED reader handles DVD short-reads.
		for wd in sorted(media.glob("wd_*.gcd")):
			if wd.stat().st_size <= 0 or wd.stat().st_size > 4 * 1024 * 1024:
				continue
			arcname = f"media/{wd.name.lower()}"
			if arcname in archive.NameToInfo:
				continue
			archive.write(wd, arcname, compress_type=zipfile.ZIP_STORED)
			staged += 1
	return staged


GC_SKY_CMPR_SIZE = 128
GC_SKY_SUFFIXES = ("rt", "bk", "lf", "ft", "up", "dn")


def gx_cmpr_encode(rgb: "np.ndarray") -> bytes:
	"""Encode an H×W×3 uint8 image (H, W multiples of 8) as GX CMPR (DXT1).

	Endpoints are the brightest and darkest pixel of each 4×4 block (enough
	for skies); blocks are laid out in 8×8 tiles of 2×2 blocks, big-endian.
	"""
	import numpy as np

	h, w, _ = rgb.shape
	px = rgb.astype(np.int32)
	# blocks[by, bx, y, x, c]
	blocks = px.reshape(h // 4, 4, w // 4, 4, 3).transpose(0, 2, 1, 3, 4)
	flat = blocks.reshape(h // 4, w // 4, 16, 3)
	lum = flat @ np.array([299, 587, 114])
	hi = np.take_along_axis(flat, lum.argmax(-1)[..., None, None], 2)[..., 0, :]
	lo = np.take_along_axis(flat, lum.argmin(-1)[..., None, None], 2)[..., 0, :]

	def to565(c):
		return ((c[..., 0] >> 3) << 11) | ((c[..., 1] >> 2) << 5) | (c[..., 2] >> 3)

	def from565(v):
		r = (v >> 11) & 31
		g = (v >> 5) & 63
		b = v & 31
		return np.stack(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)), -1)

	c0, c1 = to565(hi), to565(lo)
	swap = c0 < c1
	c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
	e0, e1 = from565(c0), from565(c1)
	pal = np.stack((e0, e1, (2 * e0 + e1) // 3, (e0 + 2 * e1) // 3), 2)
	dist = ((flat[:, :, :, None, :] - pal[:, :, None, :, :]) ** 2).sum(-1)
	idx = dist.argmin(-1)
	idx = np.where((c0 == c1)[..., None], 0, idx)
	rows = idx.reshape(h // 4, w // 4, 4, 4)
	bits = (rows[..., 0] << 6) | (rows[..., 1] << 4) | (rows[..., 2] << 2) | rows[..., 3]
	out = bytearray()
	for ty in range(0, h // 4, 2):
		for tx in range(0, w // 4, 2):
			for by, bx in ((ty, tx), (ty, tx + 1), (ty + 1, tx), (ty + 1, tx + 1)):
				out += struct.pack(">HH", int(c0[by, bx]), int(c1[by, bx]))
				out += bytes(int(b) for b in bits[by, bx])
	return bytes(out)


def gx_cmpr_encode_blocks(rgb: "np.ndarray", opaque: "np.ndarray") -> bytes:
	"""GX CMPR (DXT1) with principal-axis endpoints, 1-bit alpha when a block
	has transparent pixels. rgb is H×W×3 uint8, opaque H×W bool, both multiples
	of 8; blocks are laid out in 8×8 tiles of 2×2 blocks, big-endian."""
	import numpy as np

	h, w = opaque.shape
	bh, bw = h // 4, w // 4
	flat = rgb.astype(np.float32).reshape(bh, 4, bw, 4, 3).transpose(0, 2, 1, 3, 4).reshape(bh, bw, 16, 3)
	mask = opaque.reshape(bh, 4, bw, 4).transpose(0, 2, 1, 3).reshape(bh, bw, 16)
	weight = mask[..., None].astype(np.float32)
	count = np.maximum(weight.sum(2, keepdims=True), 1.0)
	mean = (flat * weight).sum(2, keepdims=True) / count
	diff = (flat - mean) * weight
	cov = np.einsum("...ki,...kj->...ij", diff, diff)
	axis = np.ones(cov.shape[:2] + (3,), np.float32)
	for _ in range(8):	# power iteration
		axis = np.einsum("...ij,...j->...i", cov, axis)
		axis /= np.maximum(np.linalg.norm(axis, axis=-1, keepdims=True), 1e-6)
	t = np.einsum("...ki,...i->...k", flat - mean, axis)
	lo_t = np.where(mask, t, np.inf).min(2)
	hi_t = np.where(mask, t, -np.inf).max(2)
	lo_t = np.where(np.isfinite(lo_t), lo_t, 0.0)
	hi_t = np.where(np.isfinite(hi_t), hi_t, 0.0)
	lo = np.clip(mean[:, :, 0] + axis * lo_t[..., None], 0, 255).astype(np.int32)
	hi = np.clip(mean[:, :, 0] + axis * hi_t[..., None], 0, 255).astype(np.int32)

	def to565(c):
		return ((c[..., 0] >> 3) << 11) | ((c[..., 1] >> 2) << 5) | (c[..., 2] >> 3)

	def from565(v):
		r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
		return np.stack(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)), -1)

	a, b = to565(hi), to565(lo)
	has_alpha = ~mask.all(2)
	# c0 > c1 selects four colours, c0 <= c1 three plus transparent.
	four = np.where(a > b, a, b), np.where(a > b, b, a)
	c0 = np.where(has_alpha, four[1], four[0])
	c1 = np.where(has_alpha, four[0], four[1])
	e0, e1 = from565(c0), from565(c1)
	pal4 = np.stack((e0, e1, (2 * e0 + e1) // 3, (e0 + 2 * e1) // 3), 2)
	pal3 = np.stack((e0, e1, (e0 + e1) // 2, np.full_like(e0, 100000)), 2)
	pal = np.where(has_alpha[..., None, None], pal3, pal4).astype(np.float32)
	dist = ((flat[:, :, :, None, :] - pal[:, :, None, :, :]) ** 2).sum(-1)
	idx = dist.argmin(-1)
	idx = np.where(mask, idx, 3)
	idx = np.where(((c0 == c1) & ~has_alpha)[..., None], 0, idx)
	rows = idx.reshape(bh, bw, 4, 4)
	bits = ((rows[..., 0] << 6) | (rows[..., 1] << 4) | (rows[..., 2] << 2) | rows[..., 3]).astype(np.uint8)
	out = bytearray()
	for ty in range(0, bh, 2):
		for tx in range(0, bw, 2):
			for by, bx in ((ty, tx), (ty, tx + 1), (ty + 1, tx), (ty + 1, tx + 1)):
				out += struct.pack(">HH", int(c0[by, bx]), int(c1[by, bx]))
				out += bits[by, bx].tobytes()
	return bytes(out)


GC_WT_MAGIC = b"GCWT"
GC_WT_FLAG_ALPHA = 1
GC_WT_ENTRY = struct.Struct(">16sHHBBHI")	# name, w, h, levels, flags, content tag, offset


def wt_read_miptex(data: bytes, offset: int = 0):
	"""A WAD3/BSP miptex: (name, w, h, indexed pixels, 768-byte palette), or None."""
	raw_name, w, h, m0 = struct.unpack_from("<16sIII", data, offset)
	if not m0 or not w or not h or w * h > 1 << 20:
		return None
	name = raw_name.split(b"\0")[0].decode("latin-1").lower()
	end = offset + m0 + w * h + (w * h) // 4 + (w * h) // 16 + (w * h) // 64
	count = struct.unpack_from("<H", data, end)[0]
	if count != 256 or end + 2 + 768 > len(data):
		return None
	return name, w, h, data[offset + m0:offset + m0 + w * h], data[end + 2:end + 2 + 768]


def wt_wad_search_roots(source: Path) -> list[Path]:
	"""Dirs that may hold map / Valve WADs CapFaces needs but lean staging omits.

	Lean discs skip foreign `dod_*.wad` and giant `halflife.wad` to keep tip
	searchpaths small. World-texture bake still needs those miptex so CapFaces
	hashes resolve to CMPR instead of grey stubs — pull from sibling full trees
	at bake time only (nothing is staged onto the disc).
	"""
	roots: list[Path] = []
	seen: set[Path] = set()

	def add(path: Path | None) -> None:
		if path is None:
			return
		try:
			resolved = path.resolve()
		except OSError:
			return
		if not resolved.is_dir() or resolved in seen:
			return
		seen.add(resolved)
		roots.append(resolved)

	add(source)
	parent = source.parent
	# …/dod-lean → …/dod + …/valve; smoke lean under /tmp often sits beside copies.
	for name in (source.name, "dod", "valve"):
		add(parent / name)
	for env in ("DODGC_FULL_DOD", "DODGC_WT_WAD_ROOT", "HL_VALVE_DIR"):
		raw = os.environ.get(env, "").strip()
		if raw:
			add(Path(raw))
	# Dataset fallback used by local ISO builds.
	add(Path("/mnt/datasets/dodgc-iso-build/dod"))
	add(Path("/mnt/datasets/dodgc-iso-build/valve"))
	return roots


def wt_bsp_wad_basenames(bsp: Path) -> list[str]:
	"""Basenames from the BSP worldspawn \"wad\" key (order preserved)."""
	try:
		data = bsp.read_bytes()
		off, length = struct.unpack_from("<ii", data, 4)
		ents = data[off : off + min(length, 8192)].decode("latin-1", "replace")
	except (OSError, struct.error):
		return []
	m = re.search(r'"wad"\s+"([^"]+)"', ents)
	if not m:
		return []
	names: list[str] = []
	seen: set[str] = set()
	for part in m.group(1).split(";"):
		part = part.strip().replace("\\", "/")
		if not part:
			continue
		name = part.split("/")[-1]
		key = name.lower()
		if not key.endswith(".wad") or key in seen:
			continue
		seen.add(key)
		names.append(name)
	return names


def wt_ingest_wad_file(found: dict, wad: Path) -> int:
	"""Index every miptex (type 0x43) from one WAD; first entry wins. Returns new count."""
	try:
		data = wad.read_bytes()
	except OSError:
		return 0
	if data[:4] not in (b"WAD3", b"WAD2"):
		return 0
	count, table = struct.unpack_from("<ii", data, 4)
	added = 0
	for i in range(count):
		pos, disk, size, kind, comp, _pad, raw = struct.unpack_from("<iiibbh16s", data, table + i * 32)
		if kind != 0x43 or comp:
			continue
		name = raw.split(b"\0")[0].decode("latin-1").lower()
		if name not in found:
			found[name] = (data, pos)
			added += 1
	return added


def wt_read_wads(source: Path, bsp: Path | None = None) -> dict:
	"""Every miptex (type 0x43) needed for WT bake, first WAD wins.

	Starts with `source/*.wad` (lean), then loads BSP-referenced WADs from
	sibling full trees — including oversized `halflife.wad` — so CapFaces
	hashes that lean staging dropped still land in media/wt_<map>.gct.
	"""
	found: dict = {}
	for wad in sorted(source.glob("*.wad")):
		wt_ingest_wad_file(found, wad)
	wanted = {n.lower() for n in (wt_bsp_wad_basenames(bsp) if bsp is not None else [])}
	# Always consider the common Valve + shared DoD WADs CapFaces hits.
	wanted.update({"halflife.wad", "hlbasics.wad", "decals.wad"})
	if bsp is not None:
		# Sibling map WADs listed on the BSP (dod_jagd.wad, …) even when lean
		# stripped them from the gamedir.
		wanted.update({n.lower() for n in wt_bsp_wad_basenames(bsp)})
	# Also pick up every other dod_*.wad under search roots — cheap index, encode
	# is still limited to textures the BSP actually names.
	roots = wt_wad_search_roots(source)
	loaded: set[str] = {p.name.lower() for p in source.glob("*.wad")}
	for root in roots:
		for wad in sorted(root.glob("*.wad")):
			key = wad.name.lower()
			if key in loaded:
				continue
			if key not in wanted and not key.startswith("dod_"):
				continue
			if wt_ingest_wad_file(found, wad):
				loaded.add(key)
	return found


def wt_encode(w: int, h: int, pixels: bytes, palette: bytes, alpha: bool) -> tuple[int, int, int, bytes]:
	"""Palette texture to a CMPR mip chain, power-of-two sides (GX_REPEAT
	needs them; UVs are normalised, so a resize does not move anything)."""
	import numpy as np
	from PIL import Image

	pal = np.frombuffer(palette, np.uint8).reshape(256, 3)
	idx = np.frombuffer(pixels, np.uint8).reshape(h, w)
	rgb = pal[idx]
	opaque = idx != 255 if alpha else np.ones((h, w), bool)
	tw, th = 1 << max(3, (w - 1).bit_length()), 1 << max(3, (h - 1).bit_length())
	if (tw, th) != (w, h):
		resample = Image.Resampling.NEAREST if alpha else Image.Resampling.BICUBIC
		rgb = np.asarray(Image.fromarray(rgb).resize((tw, th), resample))
		opaque = np.asarray(Image.fromarray(opaque).resize((tw, th), Image.Resampling.NEAREST))
	levels = bytearray()
	count = 0
	fcol = rgb.astype(np.float32)
	fcov = opaque.astype(np.float32)
	while True:
		levels += gx_cmpr_encode_blocks(np.clip(fcol + 0.5, 0, 255).astype(np.uint8), fcov >= 0.5)
		count += 1
		ch, cw = fcov.shape
		if min(ch, cw) <= 8:
			break
		# Box filter weighted by coverage, so a transparent pixel's colour
		# never bleeds into the edge of a cut-out.
		cover = fcov.reshape(ch // 2, 2, cw // 2, 2).sum((1, 3))
		sumc = (fcol * fcov[..., None]).reshape(ch // 2, 2, cw // 2, 2, 3).sum((1, 3))
		fcol = np.where(cover[..., None] > 0, sumc / np.maximum(cover[..., None], 1e-6), 0)
		fcov = cover / 4
	return tw, th, count, bytes(levels)


def build_music_overlays(source: Path, output: Path, cache: Path) -> list[tuple[str, Path]]:
	"""CD tracks as 22 kHz stereo WAV, media/cdNN.wav, with the cdaudio.txt
	playlist. The Gekko can't decode MP3 during play; a WAV streams from disc
	the way the announcer does. An MP3's title tag names its CD track."""
	cache.mkdir(parents=True, exist_ok=True)
	tracks = {}
	for mp3 in sorted((source / "media").glob("*.mp3")):
		probe = subprocess.run(
			["ffprobe", "-v", "error", "-show_entries", "format_tags=title", "-of", "csv=p=0", str(mp3)],
			capture_output=True, text=True,
		)
		m = re.search(r"AudioTrack (\d+)", probe.stdout)
		if m:
			tracks[int(m.group(1))] = mp3
	overlays = []
	lines = []
	for number in range(1, max(tracks, default=0) + 1):
		mp3 = tracks.get(number)
		if mp3 is None:
			lines.append("blank")
			continue
		rel = f"media/cd{number:02d}.wav"
		wav = cache / f"cd{number:02d}.wav"
		if not wav.is_file():
			subprocess.run(
				["ffmpeg", "-v", "error", "-y", "-i", str(mp3), "-ar", "22050", "-ac", "2", "-c:a", "pcm_s16le", str(wav)],
				check=True,
			)
		overlays.append((rel, wav))
		lines.append(f"/{rel}")
	playlist = output / "media" / "cdaudio.txt"
	playlist.write_text("\n".join(lines) + "\n", encoding="ascii")
	overlays.append(("media/cdaudio.txt", playlist))
	print(f"  music: {len(overlays) - 1} CD tracks as 22 kHz WAV")
	return overlays


def build_world_texture_packs(
	source: Path,
	output: Path,
	cache: Path,
	map_stems: set[str] | None = None,
) -> list[tuple[str, Path]]:
	"""media/wt_<map>.gct: each map's world textures as GX CMPR with mip
	chains, read in one go by the engine (mod_bmodel.c). Water and sky keep the
	engine's own path."""
	try:
		import numpy  # noqa: F401
		from PIL import Image  # noqa: F401
	except ImportError:
		print("warning: numpy/Pillow missing, no world texture packs")
		return []
	cache.mkdir(parents=True, exist_ok=True)
	overlays = []
	media = output / "media"
	media.mkdir(parents=True, exist_ok=True)
	total = 0
	missed_total = 0
	for bsp in sorted((source / "maps").glob("*.bsp")):
		if map_stems is not None and bsp.stem.lower() not in map_stems:
			continue
		rel = f"media/wt_{bsp.stem.lower()}.gct"
		# Preserve host-prebaked GCWT packs (e.g. Lost Coast Source→GX bake).
		# Re-deriving from a GoldSrc stub BSP would clobber CMPR world textures.
		# DoD CapFaces packs need every BSP-named diffuse — regenerate when a
		# lean prebake is missing textures that sibling WADs can still supply.
		prebaked = source / rel
		data = bsp.read_bytes()
		off, _size = struct.unpack_from("<ii", data, 4 + 2 * 8)
		count = struct.unpack_from("<i", data, off)[0]
		bsp_names: list[str] = []
		for o in struct.unpack_from(f"<{count}i", data, off + 4):
			if o < 0:
				continue
			raw = struct.unpack_from("<16s", data, off + o)[0]
			name = raw.split(b"\0")[0].decode("latin-1").lower()
			if name and name[:1] not in ("!", "*") and not name.startswith("sky"):
				bsp_names.append(name)
		wads = wt_read_wads(source, bsp)
		if prebaked.is_file() and prebaked.stat().st_size >= 32_000 and prebaked.read_bytes()[:4] == GC_WT_MAGIC:
			pre = prebaked.read_bytes()
			pre_count = struct.unpack_from(">I", pre, 4)[0]
			pre_names = {
				pre[12 + i * 28 : 12 + i * 28 + 16].split(b"\0", 1)[0].decode("latin-1").lower()
				for i in range(pre_count)
			}
			# Only preserve when every resolvable BSP diffuse is already packed.
			# A name is "resolvable" if WAD/BSP miptex exists; skip names we
			# still cannot encode (no false rebuild loop).
			name_off = {}
			for o in struct.unpack_from(f"<{count}i", data, off + 4):
				if o < 0:
					continue
				raw = struct.unpack_from("<16s", data, off + o)[0]
				n = raw.split(b"\0")[0].decode("latin-1").lower()
				if n and n not in name_off:
					name_off[n] = o
			really_missing = []
			for n in dict.fromkeys(bsp_names):
				if n in pre_names:
					continue
				o = name_off.get(n)
				if o is None:
					continue
				if wt_read_miptex(data, off + o) is not None:
					really_missing.append(n)
				elif n in wads and wt_read_miptex(*wads[n]) is not None:
					really_missing.append(n)
			if not really_missing:
				dest = output / rel
				dest.parent.mkdir(parents=True, exist_ok=True)
				shutil.copy2(prebaked, dest)
				overlays.append((rel, dest))
				total += dest.stat().st_size
				print(f"  world textures: preserve prebaked {rel} ({dest.stat().st_size} bytes)")
				continue
			print(
				f"  world textures: refresh {rel} — prebake missing {len(really_missing)} "
				f"({', '.join(really_missing[:6])}{'…' if len(really_missing) > 6 else ''})"
			)
		entries, blob = [], bytearray()
		seen = set()
		missed: list[str] = []
		for o in struct.unpack_from(f"<{count}i", data, off + 4):
			if o < 0:
				continue
			raw = struct.unpack_from("<16s", data, off + o)[0]
			name = raw.split(b"\0")[0].decode("latin-1").lower()
			if name in seen or name[:1] in ("!", "*") or name.startswith("sky"):
				continue
			seen.add(name)
			mip = wt_read_miptex(data, off + o)
			if mip is None and name in wads:
				mip = wt_read_miptex(*wads[name])
			if mip is None:
				missed.append(name)
				continue
			_, w, h, pixels, palette = mip
			alpha = name.startswith("{")
			key = hashlib.md5(bytes((alpha,)) + struct.pack("<II", w, h) + pixels + palette + b"2").hexdigest()
			item = cache / f"{key}.bin"
			if item.is_file():
				cached = item.read_bytes()
				tw, th, levels = struct.unpack_from("<HHH", cached)
				body = cached[6:]
			else:
				tw, th, levels, body = wt_encode(w, h, pixels, palette, alpha)
				item.write_bytes(struct.pack("<HHH", tw, th, levels) + body)
			entries.append((name, tw, th, levels, GC_WT_FLAG_ALPHA if alpha else 0, int(key[:4], 16), len(blob)))
			blob += body
			blob += bytes(-len(blob) & 31)
		if missed:
			missed_total += len(missed)
			print(f"  world textures: {bsp.stem} still missing {len(missed)}: {', '.join(missed[:8])}")
		if not entries:
			continue
		head = GC_WT_MAGIC + struct.pack(">II", len(entries), len(blob))
		table = b"".join(
			GC_WT_ENTRY.pack(n.encode("latin-1"), tw, th, lv, fl, tag, of) for n, tw, th, lv, fl, tag, of in entries
		)
		first = len(head) + len(table)
		pad = bytes(-first & 31)
		(output / rel).write_bytes(head + table + pad + bytes(blob))
		overlays.append((rel, output / rel))
		total += len(blob)
		print(f"  world textures: {rel} entries={len(entries)} blob={len(blob)} bytes")
	print(f"  world textures: {len(overlays)} map packs, CMPR with mipmaps, {total // (1024 * 1024)} MiB")
	if missed_total:
		print(f"  world textures: {missed_total} unresolved miptex across maps")
	return overlays


def map_sky_names(source: Path) -> set[str]:
	"""Sky names the maps use (worldspawn "skyname"), plus the default."""
	names = {"desert"}
	for bsp in (source / "maps").glob("*.bsp"):
		try:
			with bsp.open("rb") as f:
				head = f.read(12)
				off, length = struct.unpack_from("<ii", head, 4)
				f.seek(off)
				text = f.read(min(length, 4096)).decode("latin-1", "replace")
		except (OSError, struct.error):
			continue
		m = re.search(r'"skyname"\s+"([^"]+)"', text.split("}", 1)[0])
		if m:
			names.add(m.group(1).lower())
	return names


def build_sky_cmpr_overlays(source: Path, output: Path) -> list[tuple[str, Path]]:
	"""Skyboxes as 128×128 GX CMPR sides (8 KiB each) in shallow media/.

	Per-map lean discs stage media/sky_<name>*.cmp first (or gfx/env TGAs).
	Flipper draws that sky before CapFaces — prioritize shipping the map's
	skyname only (map_sky_names), not every DoD sky.
	"""
	overlays: list[tuple[str, Path]] = []
	media = output / "media"
	media.mkdir(parents=True, exist_ok=True)
	src_media = source / "media"
	names = sorted(map_sky_names(source))

	# Prefer prebuilt CMPR (stage-lean-dod / sky cache) — no Pillow round-trip.
	for name in names:
		prebuilt = []
		for suffix in GC_SKY_SUFFIXES:
			cand = src_media / f"sky_{name}{suffix}.cmp"
			if cand.is_file() and cand.stat().st_size > 0:
				prebuilt.append((suffix, cand))
		if len(prebuilt) == 6:
			for suffix, src in prebuilt:
				rel = f"media/sky_{name}{suffix}.cmp"
				dest = output / rel
				if src.resolve() != dest.resolve():
					dest.write_bytes(src.read_bytes())
				overlays.append((rel, dest))
			continue

		try:
			import numpy as np
			from PIL import Image
		except ImportError:
			print(f"warning: numpy/Pillow missing, cannot encode sky '{name}'")
			continue
		sides = []
		for suffix in GC_SKY_SUFFIXES:
			for ext in ("tga", "bmp"):
				src = source / "gfx/env" / f"{name}{suffix}.{ext}"
				if src.is_file():
					sides.append((suffix, src))
					break
		if len(sides) != 6:
			print(f"warning: incomplete skybox '{name}' (need 6 sides, have {len(sides)})")
			continue
		for suffix, src in sides:
			with Image.open(src) as img:
				img = img.convert("RGB").resize((GC_SKY_CMPR_SIZE, GC_SKY_CMPR_SIZE), Image.Resampling.LANCZOS)
				data = gx_cmpr_encode(np.asarray(img))
			rel = f"media/sky_{name}{suffix}.cmp"
			(output / rel).write_bytes(data)
			overlays.append((rel, output / rel))
	print(f"  skyboxes: {len(overlays) // 6} as GX CMPR {GC_SKY_CMPR_SIZE}x{GC_SKY_CMPR_SIZE} (maps={','.join(names) or 'none'})")
	return overlays


def lean_studio_mesh_bytes(src: Path) -> bytes | None:
	"""G289: truncate MDL before texture blobs (numtextures=0) for tip-safe BSS.

	Keeps bones/meshes/seqs; Flipper binds white soft skins under quality 0.
	Handgun drops ~134 KiB → ~60 KiB; crowbar ~47 → ~19 KiB.
	"""
	try:
		raw = src.read_bytes()
	except OSError:
		return None
	if len(raw) < 192 or raw[:4] not in (b"IDST", b"IDSQ"):
		return None
	# studiohdr_t: textureindex @184, texturedataindex @188, numtextures @180
	numtextures = struct.unpack_from("<i", raw, 180)[0]
	textureindex = struct.unpack_from("<i", raw, 184)[0]
	texturedataindex = struct.unpack_from("<i", raw, 188)[0]
	length = struct.unpack_from("<i", raw, 72)[0]
	if textureindex <= 244 or textureindex > len(raw):
		# No usable texture cursor — keep whole file if small enough.
		if length <= 0 or length > len(raw):
			length = len(raw)
		return bytes(raw[:length])
	# Cut at texture chunk; clear texture counts so loaders skip skin blobs.
	cut = textureindex
	if cut > len(raw):
		cut = len(raw)
	out = bytearray(raw[:cut])
	struct.pack_into("<i", out, 72, cut)  # length
	struct.pack_into("<i", out, 180, 0)  # numtextures
	struct.pack_into("<i", out, 184, 0)  # textureindex
	struct.pack_into("<i", out, 188, 0)  # texturedataindex
	# skinref tables often live after textures; zero if past cut
	numskinref = struct.unpack_from("<i", out, 192)[0] if len(out) >= 196 else 0
	skinindex = struct.unpack_from("<i", out, 200)[0] if len(out) >= 204 else 0
	if skinindex >= cut:
		struct.pack_into("<i", out, 192, 0)
		struct.pack_into("<i", out, 196, 0)
		struct.pack_into("<i", out, 200, 0)
	_ = (numtextures, texturedataindex, numskinref)
	return bytes(out)


def lean_studio_tex_companion_bytes(src: Path, max_edge: int = 64) -> bytes | None:
	"""Build a tip-safe *t.mdl skin atlas from an embedded-texture studio MDL.

	CapFaces DoD rifles are 300–600 KiB fully textured — too big for the 184 KiB
	studio BSS and the ~400 KiB tip heap after densify. Mesh stays lean; this
	companion downsamples indexed skins so gc_studio/<stem>t.mdl fits the
	132 KiB studio tex scratch and Mod_GCAttachStudioSkins can upload.
	"""
	try:
		raw = src.read_bytes()
	except OSError:
		return None
	if len(raw) < 244 or raw[:4] not in (b"IDST", b"IDSQ"):
		return None
	numtextures = struct.unpack_from("<i", raw, 180)[0]
	textureindex = struct.unpack_from("<i", raw, 184)[0]
	numskinref = struct.unpack_from("<i", raw, 192)[0]
	numskinfamilies = struct.unpack_from("<i", raw, 196)[0]
	skinindex = struct.unpack_from("<i", raw, 200)[0]
	if numtextures <= 0 or textureindex <= 0 or textureindex + numtextures * 80 > len(raw):
		return None

	# mstudiotexture_t is 80 bytes; pixels are width*height + 768 RGB palette.
	tex_hdrs: list[tuple[bytes, int, int, int, bytes, bytes]] = []
	for i in range(numtextures):
		off = textureindex + i * 80
		name = raw[off : off + 64]
		flags, width, height, index = struct.unpack_from("<iiii", raw, off + 64)
		if width <= 0 or height <= 0 or index < 0 or index + width * height + 768 > len(raw):
			return None
		pixels = raw[index : index + width * height]
		palette = raw[index + width * height : index + width * height + 768]
		tex_hdrs.append((name, flags, width, height, pixels, palette))

	skin_bytes = b""
	if numskinref > 0 and numskinfamilies > 0 and skinindex > 0:
		skin_size = numskinref * numskinfamilies * 2
		if skinindex + skin_size <= len(raw):
			skin_bytes = raw[skinindex : skinindex + skin_size]
		else:
			numskinref = 0
			numskinfamilies = 0
	else:
		numskinref = 0
		numskinfamilies = 0

	# Header + texture structs + skinrefs, then tightly packed downsampled blobs.
	hdr_size = 244  # sizeof(studiohdr_t) on GoldSrc
	tex_table = hdr_size
	skin_table = tex_table + numtextures * 80
	pix_cursor = skin_table + len(skin_bytes)
	out = bytearray(pix_cursor)
	out[0:4] = b"IDST"
	struct.pack_into("<i", out, 4, 10)  # STUDIO_VERSION
	out[8:72] = raw[8:72]  # model name

	for i, (name, flags, width, height, pixels, palette) in enumerate(tex_hdrs):
		nw, nh = width, height
		while nw > max_edge or nh > max_edge:
			nw = max(1, nw // 2)
			nh = max(1, nh // 2)
		if nw == width and nh == height:
			new_pixels = pixels
		else:
			new_pixels = bytearray(nw * nh)
			for y in range(nh):
				sy = min(height - 1, (y * height) // nh)
				for x in range(nw):
					sx = min(width - 1, (x * width) // nw)
					new_pixels[y * nw + x] = pixels[sy * width + sx]
			new_pixels = bytes(new_pixels)
		blob = new_pixels + palette
		off = tex_table + i * 80
		out[off : off + 64] = name
		# Drop chrome/additive; keep MASKED (0x40) so VM chromakey still floods.
		keep_flags = flags & 0x40
		struct.pack_into("<iiii", out, off + 64, keep_flags, nw, nh, pix_cursor)
		out.extend(blob)
		pix_cursor += len(blob)

	if skin_bytes:
		out[skin_table : skin_table + len(skin_bytes)] = skin_bytes

	struct.pack_into("<i", out, 72, len(out))  # length
	struct.pack_into("<i", out, 180, numtextures)
	struct.pack_into("<i", out, 184, tex_table)  # textureindex
	struct.pack_into("<i", out, 188, skin_table + len(skin_bytes))  # texturedataindex
	struct.pack_into("<i", out, 192, numskinref)
	struct.pack_into("<i", out, 196, numskinfamilies)
	struct.pack_into("<i", out, 200, skin_table if skin_bytes else 0)
	return bytes(out)


def inject_gc_studio_into_bootstrap(archive: "zipfile.ZipFile", data: Path) -> int:
	"""Add allowlisted MDLs into an existing bootstrap ZIP under gc_studio/."""
	staged = 0
	all_stems = {Path(r).stem.lower() for r in GC_STUDIO_MODELS}
	# Smoke staging may omit campaign meshes; fall back to retail valve tree.
	retail = Path(__file__).resolve().parent.parent / "Half-Life" / "valve"
	# dod-gc content trees (private network-engine often lacks Half-Life/valve).
	# Only fall back for CapFaces weapon VMs — pulling the full HL1 NPC cast
	# from content/valve tip-OOMs DoD listen (probe caen-teamvm2 free≈2.6 KiB).
	dodgc_roots = (
		Path("/home/tim/Desktop/dod-gc/content/dod"),
		Path("/home/tim/Desktop/dod-gc/content/valve"),
		Path("/run/media/tim/Expansion1/xash3d-gc/Half-Life/dod"),
	)
	dod_capfaces_stems = {
		"v_garand", "v_mp40", "v_98k", "v_tommy", "v_colt", "v_luger",
		"v_amerk", "v_spade", "v_crowbar", "w_crowbar",
		# CapFaces map props (env_model) — pull from Expansion1 / content/dod.
		"sandbags", "bulb", "hk_satchel", "furniture", "by_rubble_set",
		"arc_xer_tree2", "horse_statue", "by_canopy",
		"sherman", "jagd", "by_banister", "flags",
	}
	for relative in GC_STUDIO_MODELS:
		src = data / relative
		stem = Path(relative).stem.lower()
		if not src.is_file() and retail.is_dir():
			alt = retail / relative
			if alt.is_file():
				src = alt
		if not src.is_file() and stem in dod_capfaces_stems:
			for root in dodgc_roots:
				alt = root / relative
				if alt.is_file():
					src = alt
					break
		if not src.is_file():
			continue
		arcname = f"gc_studio/{Path(relative).name.lower()}"
		if arcname in archive.NameToInfo:
			continue
		# G381: *t.mdl companions are skin atlases (scientistt, barneyt, …).
		# Never lean-strip them — lean cut scientistt.mdl to ~244 bytes.
		# Note: stem "scientist" itself ends with 't'; match base∈allowlist.
		stem = Path(relative).stem.lower()
		is_tex_companion = stem.endswith("t") and stem[:-1] in all_stems
		# Props with their skins inside the model keep them.
		# CapFaces DoD rifles/pistols are 300–600 KiB textured; lean-strip so the
		# 184 KiB DoD studio BSS can host one team viewmodel. Skins ride a
		# downsampled gc_studio/<stem>t.mdl companion (see below).
		dod_capfaces_lean = {
			"v_garand", "v_mp40", "v_98k", "v_tommy", "v_colt", "v_luger",
			"v_amerk", "v_spade",
			# Other CapFaces map props stay lean-stripped for tip maps.
			"bulb", "hk_satchel", "furniture", "by_rubble_set",
			"arc_xer_tree2", "horse_statue", "by_canopy",
			# Avalanche env_models: full-skin MDLs (sherman≈294 KiB) miss the
			# 184–288 KiB DoD studio BSS and tip≈6 KiB heap (ava-prop-2
			# promote ok=0/4). Lean mesh + *t atlas = full fidelity in BSS.
			"sandbags", "sherman", "jagd", "by_banister", "flags",
		}
		dod_full_prop_skins = set()  # avalanche props moved to lean+*t
		keep_skins = stem in ("forklift", "loader", "filecabinet", "nihilanth", "baby_headcrab") \
			or stem in dod_full_prop_skins \
			or (stem.startswith("v_") and stem not in dod_capfaces_lean)
		# Replace stale full-skin avalanche props already in the bootstrap.
		if arcname in archive.NameToInfo and stem in dod_capfaces_lean:
			del archive.NameToInfo[arcname]
			archive.filelist = [zi for zi in archive.filelist if zi.filename != arcname]
		payload = None if is_tex_companion or keep_skins else lean_studio_mesh_bytes(src)
		if payload is None:
			archive.write(src, arcname, compress_type=zipfile.ZIP_STORED)
			if is_tex_companion:
				print(f"  studio skins {Path(relative).name}: {src.stat().st_size} bytes (full)")
		else:
			archive.writestr(arcname, payload, compress_type=zipfile.ZIP_STORED)
			print(
				f"  lean studio {Path(relative).name}: {src.stat().st_size} → {len(payload)} bytes"
			)
		staged += 1
		# CapFaces rifle/pistol + Avalanche prop skin atlas — mesh is lean;
		# Flipper uploads from gc_studio/<stem>t.mdl after promote.
		if stem in dod_capfaces_lean:
			tex_arc = f"gc_studio/{stem}t.mdl"
			if tex_arc in archive.NameToInfo:
				del archive.NameToInfo[tex_arc]
				archive.filelist = [zi for zi in archive.filelist if zi.filename != tex_arc]
			# Prefer 128² for rifles; Avalanche props stay ≤64² so Flipper
			# Image_LoadMDL fits tip≈34 KiB at entity spawn (ava-prop-4
			# banister 128² OOM → 2x2 fallback).
			prop_tex64 = {
				"sandbags", "sherman", "jagd", "by_banister", "flags",
			}
			edge = 64 if stem in prop_tex64 else 128
			tex_payload = lean_studio_tex_companion_bytes(src, max_edge=edge)
			if tex_payload and len(tex_payload) > 96 * 1024:
				tex_payload = lean_studio_tex_companion_bytes(src, max_edge=64)
			if tex_payload:
				archive.writestr(tex_arc, tex_payload, compress_type=zipfile.ZIP_STORED)
				print(
					f"  lean studio tex {stem}t.mdl: {src.stat().st_size} → {len(tex_payload)} bytes"
				)
				staged += 1
	return staged


def lean_hl_spr_bytes(src: Path, scale: int = 2) -> bytes | None:
	"""Nearest-neighbor downsample a single-frame HL SPR (G173/G174 lean HUD).

	G187: for SPR_ALPHATEST sheets, prefer any non-255 texel in the scale×scale
	block so thin crosshair strokes survive 128→64 (plain NN often empties them).
	"""
	import math

	try:
		d = src.read_bytes()
	except OSError:
		return None
	if len(d) < 44 or d[:4] != b"IDSP":
		return None
	ver = struct.unpack_from("<i", d, 4)[0]
	if ver != 2:
		return None
	typ = struct.unpack_from("<i", d, 8)[0]
	tex = struct.unpack_from("<i", d, 12)[0]
	face = struct.unpack_from("<i", d, 32)[0]
	sync = struct.unpack_from("<i", d, 36)[0]
	ncolors = struct.unpack_from("<h", d, 40)[0]
	if ncolors <= 0 or ncolors > 256:
		return None
	pal_off = 42
	frame_off = pal_off + ncolors * 3
	if frame_off + 20 > len(d):
		return None
	ftype = struct.unpack_from("<i", d, frame_off)[0]
	if ftype != 0:
		return None
	ox, oy, w, h = struct.unpack_from("<iiii", d, frame_off + 4)
	pix_off = frame_off + 20
	if w <= 0 or h <= 0 or pix_off + w * h > len(d):
		return None
	if scale < 1 or (w % scale) or (h % scale):
		return None
	nw, nh = w // scale, h // scale
	pix = d[pix_off : pix_off + w * h]
	out_pix = bytearray(nw * nh)
	# texFormat 3 = SPR_ALPHATEST — index 255 is transparent.
	preserve_ink = tex == 3
	for y in range(nh):
		sy = y * scale
		for x in range(nw):
			sx = x * scale
			sample = pix[sy * w + sx]
			if preserve_ink and sample == 255:
				found = 255
				for dy in range(scale):
					row = (sy + dy) * w
					for dx in range(scale):
						p = pix[row + sx + dx]
						if p != 255:
							found = p
							break
					if found != 255:
						break
				sample = found
			out_pix[y * nw + x] = sample
	br = math.sqrt((nw * 0.5) ** 2 + (nh * 0.5) ** 2)
	out = bytearray()
	out += b"IDSP"
	out += struct.pack("<iii", 2, typ, tex)
	out += struct.pack("<f", br)
	out += struct.pack("<ii", nw, nh)
	out += struct.pack("<iii", 1, face, sync)
	out += struct.pack("<h", ncolors)
	out += d[pal_off:frame_off]
	out += struct.pack("<i", 0)
	out += struct.pack("<iiii", ox // scale, oy // scale, nw, nh)
	out += out_pix
	return bytes(out)


def inject_gc_font_into_bootstrap(archive: "zipfile.ZipFile", data: Path) -> int:
	"""G296: tip-safe downsampled CREDITSFONT (128×32) — full wad tip at VidInit."""
	wad = data / "gfx.wad"
	if not wad.is_file():
		return 0
	arcname = "gfx/gc_creditsfont.fnt"
	if arcname in archive.NameToInfo:
		return 0
	try:
		blob = wad.read_bytes()
	except OSError:
		return 0
	if len(blob) < 12:
		return 0
	magic, count, doff = struct.unpack_from("<4sii", blob, 0)
	if magic not in (b"WAD2", b"WAD3") or count < 1:
		return 0
	for i in range(count):
		off = doff + i * 32
		if off + 32 > len(blob):
			break
		filepos, disksize, _size, _typ, compression, _pad, name = struct.unpack_from(
			"<iiibbh16s", blob, off
		)
		n = name.split(b"\0", 1)[0].decode("latin-1", "replace")
		if n.upper() != "CREDITSFONT" or compression:
			continue
		if filepos < 0 or disksize < 1024 or filepos + disksize > len(blob):
			return 0
		lump = bytearray(blob[filepos : filepos + disksize])
		# qfont_t: 4 ints + 256 charinfo + data[4]  → fin at sizeof-4
		qfont_size = 4 * 4 + 256 * 4 + 4
		fin_off = qfont_size - 4
		src_w, src_h = 256, 64
		if fin_off + src_w * src_h + 2 + 768 > len(lump):
			return 0
		pix = lump[fin_off : fin_off + src_w * src_h]
		pal = lump[fin_off + src_w * src_h :]
		dst_w, dst_h = 128, 32
		out = bytearray(dst_w * dst_h)
		# Brightest of each 2x2, not point sampling: CREDITSFONT strokes are
		# 1 px wide and every-other-pixel sampling dropped them (garbled intro
		# credits). Brightness comes from the palette (after the 2-byte count);
		# the index order is not a ramp.
		lum = [sum(pal[2 + 3 * k : 5 + 3 * k]) for k in range(256)]
		for y in range(dst_h):
			for x in range(dst_w):
				o = (y * 2) * src_w + (x * 2)
				out[y * dst_w + x] = max(
					(pix[o], pix[o + 1], pix[o + src_w], pix[o + src_w + 1]),
					key=lambda idx: lum[idx],
				)
		_width, _height, rowcount, rowheight = struct.unpack_from("<iiii", lump, 0)
		struct.pack_into(
			"<iiii", lump, 0, _width, dst_h, rowcount, max(1, rowheight // 2)
		)
		for gi in range(256):
			so, cw = struct.unpack_from("<hh", lump, 16 + gi * 4)
			sx, sy = so % src_w, so // src_w
			nso = (sy // 2) * dst_w + (sx // 2)
			struct.pack_into("<hh", lump, 16 + gi * 4, nso, max(1, cw // 2))
		payload = bytes(lump[:fin_off]) + bytes(out) + bytes(pal)
		if len(payload) > 8 * 1024:
			return 0
		archive.writestr(arcname, payload, compress_type=zipfile.ZIP_STORED)
		print(f"  lean font CREDITSFONT: → {arcname} ({len(payload)} bytes, {dst_w}x{dst_h})")
		# Full-resolution copy for the menu New Game intro credits, which are
		# unreadable at half size. The engine loads it only on that route.
		full = bytes(blob[filepos : filepos + disksize])
		archive.writestr("gfx/gc_creditsfont256.fnt", full, compress_type=zipfile.ZIP_STORED)
		print(f"  full font CREDITSFONT: → gfx/gc_creditsfont256.fnt ({len(full)} bytes, 256x{src_h})")
		return 1
	return 0


def inject_gc_hud_into_bootstrap(archive: "zipfile.ZipFile", data: Path) -> int:
	"""Add allowlisted HUD sprites into bootstrap under their disc paths."""
	# G173/G174/G288: downsample fat sheets to ~5 KiB under gc_* aliases.
	# Digit/ammo sheets are multi-frame; lean_hl_spr_bytes keeps only frame 0 and
	# CapFaces HUD drew empty glyphs (hud6/hud7). Prefer full retail under gc_*
	# when tip can afford it (~17–66 KiB). Crosshairs stay NN-downsampled.
	lean_scale = {
		"gc_crosshairs.spr": 2,  # 128→64, single-frame ALPHATEST
	}
	full_copy = {
		"gc_320hud1.spr",
		"gc_320hud2.spr",
		"gc_320_train.spr",
		"gc_320_pain.spr",
	}
	staged = 0
	for entry in GC_HUD_SPRITES:
		if isinstance(entry, tuple):
			src_rel, arc_rel = entry
		else:
			src_rel = arc_rel = entry
		src = data / src_rel
		if not src.is_file():
			continue
		arcname = arc_rel.lower()
		if arcname in archive.NameToInfo:
			continue
		arc_base = Path(arc_rel).name.lower()
		if arc_base in full_copy:
			archive.write(src, arcname, compress_type=zipfile.ZIP_STORED)
		elif arc_base in lean_scale:
			payload = lean_hl_spr_bytes(src, scale=lean_scale[arc_base])
			if not payload:
				continue
			archive.writestr(arcname, payload, compress_type=zipfile.ZIP_STORED)
		else:
			archive.write(src, arcname, compress_type=zipfile.ZIP_STORED)
		staged += 1
	return staged


def stage_retail_data(source: Path, output: Path) -> tuple[Path, int]:
	"""
	Copy retail Half-Life assets into a GameCube-friendly staging tree.

	Source files are never modified. Unsupported formats are omitted from the
	staged disc payload, and strict-case directories are normalized to lowercase.
	"""
	output.mkdir(parents=True, exist_ok=True)
	skipped = 0

	for root, _dirs, files in os.walk(source):
		rel_root = os.path.relpath(root, source)
		if rel_root == ".":
			rel_root = ""

		for filename in files:
			if should_skip_retail_asset(filename):
				skipped += 1
				continue

			source_file = Path(root) / filename
			relative = staged_retail_relative(rel_root, filename)
			destination = output / relative
			destination.parent.mkdir(parents=True, exist_ok=True)
			if destination.exists():
				continue
			shutil.copy2(source_file, destination)

	return output, skipped


def extract_wad_lump(wad_path: Path, output: Path, lump_name: str, relative: str) -> None:
	if not wad_path.is_file():
		return

	with wad_path.open("rb") as wad:
		header = wad.read(12)
		if len(header) != 12:
			return
		magic, lump_count, dir_offset = struct.unpack("<4sii", header)
		if magic not in (b"WAD2", b"WAD3") or lump_count < 1:
			return

		wad.seek(dir_offset)
		for _ in range(lump_count):
			entry = wad.read(32)
			if len(entry) != 32:
				return
			filepos, disksize, _size, _type, compression, name = struct.unpack(
				"<iiibbxx16s", entry
			)
			name = name.split(b"\0", 1)[0].decode("latin-1")
			if name.lower() != lump_name.lower() or compression:
				continue

			wad.seek(filepos)
			data = wad.read(disksize)
			destination = output / relative
			destination.parent.mkdir(parents=True, exist_ok=True)
			destination.write_bytes(data)
			return


def stage_probe_tas(output: Path, tas_name: str, *, scripts_root: Path | None = None) -> None:
	"""Copy scripts/tas/<name>.tas → valve/tas/<name>.tas on the staged disc root."""
	root = scripts_root or Path(__file__).resolve().parent
	src = root / "tas" / f"{tas_name}.tas"
	if not src.is_file():
		raise FileNotFoundError(f"TAS script does not exist: {src}")
	dest_dir = output / "tas"
	dest_dir.mkdir(parents=True, exist_ok=True)
	shutil.copy2(src, dest_dir / f"{tas_name}.tas")


def write_smoke_overrides(
	output: Path,
	smoke_map: str,
	*,
	newgame: bool = False,
	newsaveload: bool = False,
	configroundtrip: bool = False,
	saveinterrupt: bool = False,
	video_mode: str | None = None,
	world_render: bool = False,
	phasetest: str | None = None,
	changelevel: str | None = None,
	landmark: str | None = None,
	changelevel2: str | None = None,
	landmark2: str | None = None,
	changelevel3: str | None = None,
	landmark3: str | None = None,
	leanpvs: bool = False,
	fullphysics: bool = False,
	tas: str | None = None,
	dumpframes: bool = False,
) -> None:
	(output / "valve.rc").write_text("stuffcmds\n", encoding="ascii")
	(output / "config.cfg").write_text("\n", encoding="ascii")
	(output / "autoexec.cfg").write_text("\n", encoding="ascii")
	lines = [f"map {Path(smoke_map).stem}"]
	# Swiss SD smoke only: return to loader after markers so FAT shutdown is written.
	# Do not bake into DumpFrames/newgame Flipper probes — SwissQuit can race tip-safe
	# changelevel/DumpFrames and is irrelevant on disc-only Dolphin.
	if os.environ.get("GC_SMOKE_SWISSQUIT", "").strip().lower() in ("1", "true", "yes", "on"):
		lines.append("swissquit")
	if newgame:
		lines.append("newgame")
	if newsaveload:
		lines.append("newsaveload")
	if configroundtrip:
		lines.append("configroundtrip")
	if saveinterrupt:
		lines.append("saveinterrupt")
	if video_mode:
		lines.append(f"video {video_mode}")
	if changelevel:
		# G68: enable New Game PVS/present/changelevel path on the smoke map.
		lines.append("newgame")
	if world_render:
		lines.append("gcworldrender")
	if phasetest:
		lines.append(f"phasetest {phasetest}")
	if leanpvs:
		lines.append("leanpvs")
	if fullphysics:
		lines.append("fullphysics")
	if tas:
		lines.append(f"tas {tas}")
	if dumpframes:
		lines.append("dumpframes")
	if changelevel and landmark:
		lines.append(f"changelevel {Path(changelevel).stem} {landmark}")
	elif changelevel:
		lines.append(f"changelevel {Path(changelevel).stem}")
	elif landmark:
		lines.append(f"landmark {landmark}")
	if changelevel2 and landmark2:
		lines.append(f"changelevel2 {Path(changelevel2).stem} {landmark2}")
	elif changelevel2:
		lines.append(f"changelevel2 {Path(changelevel2).stem}")
	elif landmark2:
		lines.append(f"landmark2 {landmark2}")
	if changelevel3 and landmark3:
		lines.append(f"changelevel3 {Path(changelevel3).stem} {landmark3}")
	elif changelevel3:
		lines.append(f"changelevel3 {Path(changelevel3).stem}")
	elif landmark3:
		lines.append(f"landmark3 {landmark3}")
	if os.environ.get("XASH_GC_PROBE_PROFILE") == "1":
		lines.append("profile")
	if os.environ.get("XASH_GC_PROBE_AUTOCAMPAIGN"):
		lines.append("autocampaign " + os.environ["XASH_GC_PROBE_AUTOCAMPAIGN"])
	if os.environ.get("XASH_GC_PROBE_NATIVETEX") == "1":
		lines.append("nativetex")
	(output / "gamecube.cfg").write_text("\n".join(lines) + "\n", encoding="ascii")
	media = output / "media"
	media.mkdir(exist_ok=True)
	(media / "StartupVids.txt").write_text("", encoding="ascii")


def write_probe_newgame_override(
	output: Path,
	newsaveload: bool = False,
	configroundtrip: bool = False,
	saveinterrupt: bool = False,
	video_mode: str | None = None,
	phasetest: str | None = None,
	changelevel: str | None = None,
	landmark: str | None = None,
	changelevel2: str | None = None,
	landmark2: str | None = None,
	changelevel3: str | None = None,
	landmark3: str | None = None,
	smoke_map: str | None = None,
	leanpvs: bool = False,
	fullphysics: bool = False,
	tas: str | None = None,
	dumpframes: bool = False,
) -> None:
	lines = []
	# G68/G100: bake map so -gcmap+newgame early changelevel path can plant inventory.
	if smoke_map:
		lines.append(f"map {Path(smoke_map).stem}")
	lines.append("newgame")
	if newsaveload:
		lines.append("newsaveload")
	if configroundtrip:
		lines.append("configroundtrip")
	if saveinterrupt:
		lines.append("saveinterrupt")
	if video_mode:
		lines.append(f"video {video_mode}")
	if phasetest:
		lines.append(f"phasetest {phasetest}")
	if leanpvs:
		lines.append("leanpvs")
	if fullphysics:
		lines.append("fullphysics")
	if tas:
		lines.append(f"tas {tas}")
	if dumpframes:
		lines.append("dumpframes")
	if changelevel and landmark:
		lines.append(f"changelevel {Path(changelevel).stem} {landmark}")
	elif changelevel:
		lines.append(f"changelevel {Path(changelevel).stem}")
	elif landmark:
		lines.append(f"landmark {landmark}")
	if changelevel2 and landmark2:
		lines.append(f"changelevel2 {Path(changelevel2).stem} {landmark2}")
	elif changelevel2:
		lines.append(f"changelevel2 {Path(changelevel2).stem}")
	elif landmark2:
		lines.append(f"landmark2 {landmark2}")
	if changelevel3 and landmark3:
		lines.append(f"changelevel3 {Path(changelevel3).stem} {landmark3}")
	elif changelevel3:
		lines.append(f"changelevel3 {Path(changelevel3).stem}")
	elif landmark3:
		lines.append(f"landmark3 {landmark3}")
	if os.environ.get("XASH_GC_PROBE_PROFILE") == "1":
		lines.append("profile")
	if os.environ.get("XASH_GC_PROBE_AUTOCAMPAIGN"):
		lines.append("autocampaign " + os.environ["XASH_GC_PROBE_AUTOCAMPAIGN"])
	if os.environ.get("XASH_GC_PROBE_NATIVETEX") == "1":
		lines.append("nativetex")
	(output / "gamecube.cfg").write_text("\n".join(lines) + "\n", encoding="ascii")


def write_probe_phasetest_override(output: Path, phase: str) -> None:
	"""Menu/retail boot with an intentional G82 phase fault (no map/newgame)."""
	(output / "gamecube.cfg").write_text(f"phasetest {phase}\n", encoding="ascii")


def write_startup_vids(output: Path) -> None:
	movies = [
		relative
		for relative in SMOKE_INTRO_MEDIA
		if (output / relative).is_file()
	]
	if not movies:
		return

	media = output / "media"
	media.mkdir(exist_ok=True)
	(media / "StartupVids.txt").write_text(
		"".join(f"{movie}\n" for movie in movies),
		encoding="ascii",
	)


def build_logo_gcvid_companion(source: Path, output: Path) -> Path | None:
	"""Create a lightweight GameCube-native companion for the retail menu logo movie."""
	source_movie = source / "media/logo.avi"
	if not source_movie.is_file():
		return None

	output_movie = output / "media/logo.gcvid"
	build_gcvid_companion(
		source_movie,
		output_movie,
		width=GCVID_LOGO_WIDTH,
		height=GCVID_LOGO_HEIGHT,
		fps_num=GCVID_LOGO_FPS_NUM,
		fps_den=GCVID_LOGO_FPS_DEN,
		still_frame_index=GCVID_LOGO_STILL_FRAME_INDEX,
		rgb565=True,
	)
	return output_movie


def build_c0a0_prefetch_manifest(source: Path, output: Path) -> Path:
	"""Emit a deterministic first-map read-ahead manifest.

	The runtime can consume this during menu idle without walking the retail
	filesystem.  Keep it as plain ASCII so it is cheap to inspect and safe to
	stage in the bootstrap overlay; ordering is intentional and stable.
	"""
	priority = (
		"maps/c0a0.bsp",
		"maps/c0a0a.bsp",
		"models/player.mdl",
		"models/barney.mdl",
		"models/scientist.mdl",
		"models/zombiet.mdl",
		"sound/tride/c0a0_tr_gmorn.wav",
		"sound/tride/c0a0_tr_time.wav",
		"sound/plats/ttrain1.wav",
	)
	entries: list[str] = []
	seen: set[str] = set()
	for relative in priority:
		path = source / relative
		if path.is_file():
			entries.append(f"{relative} {path.stat().st_size}\n")
			seen.add(relative)
	# Include nearby first-map assets in lexical order after the critical set.
	for root_name in ("models", "sprites", "sound"):
		root = source / root_name
		if not root.is_dir():
			continue
		for path in sorted(root.rglob("*"), key=lambda item: item.as_posix().lower()):
			if not path.is_file():
				continue
			relative = path.relative_to(source).as_posix()
			if relative in seen or not any(token in relative.lower() for token in ("c0a0", "train", "tride", "barney", "scientist")):
				continue
			entries.append(f"{relative} {path.stat().st_size}\n")
			seen.add(relative)
	manifest = output / "media" / "c0a0.prefetch"
	manifest.parent.mkdir(parents=True, exist_ok=True)
	manifest.write_text("# Xash3D GameCube prefetch v1\n" + "".join(entries), encoding="ascii")
	return manifest


def build_c0a0_bsp_metadata(source: Path, output: Path) -> tuple[tuple[str, Path], ...]:
	"""Add validated lump indexes for the opening tram transition chain."""
	script = Path(__file__).with_name("generate-gc-bsp-meta.py")
	result: list[tuple[str, Path]] = []
	for map_name in ("c0a0", "c0a0a", "c0a0b", "c0a0b1", "c0a0b2", "c0a0c", "c0a0d", "c0a0e"):
		source_map = source / "maps" / f"{map_name}.bsp"
		if not source_map.is_file():
			continue
		metadata = output / "media" / f"{map_name}.gcmeta"
		subprocess.run([sys.executable, str(script), str(source_map), str(metadata)], check=True)
		result.append((f"media/{map_name}.gcmeta", metadata))
	return tuple(result)


GC_RETAIL_VALVE_RC = """// GameCube retail boot (read-only disc)
exec autoexec.cfg
stuffcmds
"""

def create_retail_boot_overlays(
	source: Path,
	output: Path,
	include_startup_vids: bool = True,
) -> tuple[tuple[str, Path], ...]:
	"""Overlay read-only retail boot files without mutating the source tree."""
	output.mkdir(parents=True, exist_ok=True)

	(output / "config.cfg").write_text("\n", encoding="ascii")
	(output / "autoexec.cfg").write_text("\n", encoding="ascii")
	(output / "mainui.cfg").write_text("\n", encoding="ascii")
	(output / "valve.rc").write_text(GC_RETAIL_VALVE_RC, encoding="ascii")

	overlays: list[tuple[str, Path]] = [
		("config.cfg", output / "config.cfg"),
		("autoexec.cfg", output / "autoexec.cfg"),
		("mainui.cfg", output / "mainui.cfg"),
		("valve.rc", output / "valve.rc"),
	]

	media = output / "media"
	media.mkdir(exist_ok=True)
	prefetch_manifest = build_c0a0_prefetch_manifest(source, output)
	overlays.append(("media/c0a0.prefetch", prefetch_manifest))
	overlays.extend(build_c0a0_bsp_metadata(source, output))
	logo_gcvid = build_logo_gcvid_companion(source, output)
	if logo_gcvid is not None:
		overlays.append(("media/logo.gcvid", logo_gcvid))
	startup_vids = media / "StartupVids.txt"

	# G278: libogc ISO9660 directory walks often miss deep sound/tride/*.wav.
	# Mirror the two audiomm VO clips (and tram rumble) into shallow media/.
	intro_vo = (
		("media/c0a0_tr_gmorn.wav", source / "sound/tride/c0a0_tr_gmorn.wav"),
		("media/c0a0_tr_time.wav", source / "sound/tride/c0a0_tr_time.wav"),
		("media/ttrain1.wav", source / "sound/plats/ttrain1.wav"),
		# The rest of the ride's announcer (c0a0a-c0a0e ambient_generics),
		# streamed by the engine's S_GCStreamVoice.
		*((f"media/c0a0_tr_{n}.wav", source / f"sound/tride/c0a0_tr_{n}.wav")
			for n in ("dest", "noeat", "emerg", "tourn", "jobs", "haz", "arrive", "exit")),
	)
	overlays.extend(build_sky_cmpr_overlays(source, output))
	overlays.extend(build_music_overlays(source, output, Path(__file__).resolve().parents[1] / "build" / "gc-music-cache"))
	overlays.extend(build_world_texture_packs(source, output, Path(__file__).resolve().parents[1] / "build" / "gc-wt-cache"))
	# Offline CapFaces packs (media/cf_<map>.gcp) — load-time DL for 60 FPS Flipper.
	from bake_capfaces_pack import build_capfaces_packs  # type: ignore

	overlays.extend(
		build_capfaces_packs(
			source,
			output,
			Path(__file__).resolve().parents[1] / "build" / "gc-cf-cache",
		)
	)
	for dest_rel, src_wav in intro_vo:
		if not src_wav.is_file():
			continue
		dest = output / dest_rel
		dest.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(src_wav, dest)
		overlays.append((dest_rel, dest))

	if not include_startup_vids:
		startup_vids.write_text("", encoding="ascii")
		overlays.append(("media/StartupVids.txt", startup_vids))
		return tuple(overlays)

	movies = [
		relative
		for relative in SMOKE_INTRO_MEDIA
		if (source / relative).is_file()
	]
	if movies:
		startup_vids.write_text("".join(f"{movie}\n" for movie in movies), encoding="ascii")
		overlays.append(("media/StartupVids.txt", startup_vids))
		for relative in movies:
			source_movie = source / relative
			if not source_movie.is_file():
				continue
			gcvid_name = Path(relative).with_suffix(".gcvid").name
			gcvid_output = media / gcvid_name
			build_gcvid_companion(source_movie, gcvid_output, rgb565=True)
			overlays.append((f"media/{gcvid_name}", gcvid_output))
			gcpcm_name = Path(relative).with_suffix(".gcpcm").name
			gcpcm_output = media / gcpcm_name
			if build_gcpcm_companion(source_movie, gcpcm_output):
				overlays.append((f"media/{gcpcm_name}", gcpcm_output))

	return tuple(overlays)


def create_startup_vids_overlay(
	source: Path,
	output: Path,
	include_startup_vids: bool = True,
) -> tuple[tuple[str, Path], ...]:
	return create_retail_boot_overlays(source, output, include_startup_vids)


def build_gcvid_companion(
	source_movie: Path,
	output_movie: Path,
	*,
	width: int = GCVID_WIDTH,
	height: int = GCVID_HEIGHT,
	fps_num: int = GCVID_FPS_NUM,
	fps_den: int = GCVID_FPS_DEN,
	still_frame_index: int = GCVID_STILL_FRAME_INDEX,
	rgb565: bool = False,
) -> None:
	ffmpeg = shutil.which("ffmpeg")
	if ffmpeg is None:
		raise FileNotFoundError("ffmpeg is required to build .gcvid intro companions")

	raw_frames = subprocess.run(
		[
			ffmpeg,
			"-v", "error",
			"-i", str(source_movie),
			"-an",
			"-vf", f"fps={fps_num}/{fps_den},scale={width}:{height},format=bgra",
			"-pix_fmt", "bgra",
			"-f", "rawvideo",
			"-vsync", "cfr",
			"-",
		],
		capture_output=True,
		check=True,
	)
	frame_size = width * height * 4
	if len(raw_frames.stdout) % frame_size != 0:
		raise ValueError(f"ffmpeg raw frame size mismatch for {source_movie}")
	actual_frames = len(raw_frames.stdout) // frame_size
	if actual_frames <= 0:
		raise ValueError(f"ffmpeg produced no frames for {source_movie}")

	tile_size = GCVID_TILE_SIZE
	if width % GCVID_TILE_SIZE != 0 or height % GCVID_TILE_SIZE != 0:
		tile_size = 0

	frames = [
		raw_frames.stdout[i * frame_size:(i + 1) * frame_size]
		for i in range(actual_frames)
	]
	still_frame_index = min(still_frame_index, actual_frames - 1)
	offsets: list[int] = []
	packets = bytearray()
	previous: bytes | None = None
	tiles_x = width // tile_size if tile_size else 0
	tiles_y = height // tile_size if tile_size else 0

	def convert_frame(frame: bytes) -> bytes:
		if not rgb565:
			return frame
		converted = bytearray(width * height * 2)
		for pixel in range(0, len(frame), 4):
			b = frame[pixel + 0]
			g = frame[pixel + 1]
			r = frame[pixel + 2]
			rgb565_pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
			converted[pixel // 2:pixel // 2 + 2] = struct.pack("<H", rgb565_pixel)
		return bytes(converted)

	def tile_bytes(frame: bytes, tile_x: int, tile_y: int) -> bytes:
		tile = bytearray()
		for row in range(tile_size):
			start = ((tile_y * tile_size + row) * width + tile_x * tile_size) * 2
			tile.extend(frame[start:start + tile_size * 2])
		return bytes(tile)

	for frame_index, source_frame in enumerate(frames):
		current = convert_frame(source_frame)
		offsets.append(len(packets))
		if frame_index == 0 or not tile_size:
			packets.append(GCVID_KEYFRAME)
			packets.extend(current)
		else:
			changed: list[tuple[int, bytes]] = []
			for tile_y in range(tiles_y):
				for tile_x in range(tiles_x):
					tile = tile_bytes(current, tile_x, tile_y)
					if tile != tile_bytes(previous, tile_x, tile_y):
						changed.append((tile_y * tiles_x + tile_x, tile))
			packets.append(GCVID_DELTAFRAME)
			packets.extend(struct.pack("<H", len(changed)))
			for tile_index, tile in changed:
				packets.extend(struct.pack("<H", tile_index))
				packets.extend(tile)
		previous = current

	header = bytearray()
	header.extend(GCVID_MAGIC)
	header.extend(struct.pack("<I", width))
	header.extend(struct.pack("<I", height))
	header.extend(struct.pack("<I", fps_num))
	header.extend(struct.pack("<I", fps_den))
	header.extend(struct.pack("<I", actual_frames))
	flags = tile_size
	if not rgb565:
		flags |= GCVID_FLAG_BGRA32
	header.extend(struct.pack("<I", flags))
	for offset in offsets:
		header.extend(struct.pack("<I", offset))

	output_movie.parent.mkdir(parents=True, exist_ok=True)
	output_movie.write_bytes(bytes(header) + bytes(packets))
	print(
		f"Built delta GCVID {output_movie.name} "
		f"({width}x{height}, duration {actual_frames} frames, "
		f"{'rgb565' if rgb565 else 'bgra32'})"
	)


def build_gcpcm_companion(source_movie: Path, output_audio: Path) -> bool:
	"""Convert movie audio to the GameCube mixer format: 48 kHz stereo s16le."""
	ffmpeg = shutil.which("ffmpeg")
	if ffmpeg is None:
		raise FileNotFoundError("ffmpeg is required to build .gcpcm intro companions")
	raw_audio = subprocess.run(
		[
			ffmpeg, "-v", "error", "-i", str(source_movie), "-map", "0:a:0",
			"-ar", str(GCPCM_RATE), "-ac", str(GCPCM_CHANNELS),
			"-f", "s16le", "-acodec", "pcm_s16le", "-",
		], capture_output=True,
	)
	if raw_audio.returncode != 0 and b"matches no streams" in raw_audio.stderr:
		print(f"Skipped native GCPCM {output_audio.name}: source has no audio stream")
		return False
	if raw_audio.returncode != 0:
		raise subprocess.CalledProcessError(raw_audio.returncode, raw_audio.args, raw_audio.stdout, raw_audio.stderr)
	if not raw_audio.stdout or len(raw_audio.stdout) % (GCPCM_CHANNELS * GCPCM_WIDTH):
		raise ValueError(f"ffmpeg produced invalid PCM audio for {source_movie}")
	# The runtime passes this buffer directly to the native-endian mixer and
	# ASND's VOICE_STEREO_16BIT_BE path. Keep the metadata little-endian, but
	# store each signed-16 PCM sample in GameCube big-endian byte order.
	pcm = bytearray(raw_audio.stdout)
	for offset in range(0, len(pcm), GCPCM_WIDTH):
		pcm[offset], pcm[offset + 1] = pcm[offset + 1], pcm[offset]
	header = GCPCM_MAGIC + struct.pack(
		"<IHHI", GCPCM_RATE, GCPCM_CHANNELS, GCPCM_WIDTH, len(pcm)
	)
	output_audio.parent.mkdir(parents=True, exist_ok=True)
	output_audio.write_bytes(header + pcm)
	print(f"Built native GCPCM {output_audio.name} ({len(pcm)} PCM bytes, 48 kHz stereo s16be/GameCube-native)")
	return True


def build_intro_gcvid_companions(output: Path) -> None:
	for relative in SMOKE_INTRO_MEDIA:
		source_movie = output / relative
		if not source_movie.is_file():
			continue
		gcvid = source_movie.with_suffix(".gcvid")
		build_gcvid_companion(source_movie, gcvid, rgb565=True)
		gcpcm = source_movie.with_suffix(".gcpcm")
		build_gcpcm_companion(source_movie, gcpcm)
	build_logo_gcvid_companion(output, output)


def smoke_map_resources(map_path: Path) -> set[str]:
	blob = map_path.read_bytes()
	resources: set[str] = set()

	for match in re.finditer(rb'[\w./-]+\.(?:mdl|spr|wav|wad)', blob, re.IGNORECASE):
		name = match.group(0).decode("latin-1").replace("\\", "/").lower()
		if name.endswith(".wav") and not name.startswith("sound/"):
			name = f"sound/{name}"
		elif name.endswith(".wad"):
			name = Path(name).name
		resources.add(name)

	return resources


def stage_smoke_data(
	source: Path,
	output: Path,
	smoke_map: str,
	*,
	newgame: bool = False,
	newsaveload: bool = False,
	configroundtrip: bool = False,
	saveinterrupt: bool = False,
	video_mode: str | None = None,
	world_render: bool = False,
	phasetest: str | None = None,
	changelevel: str | None = None,
	landmark: str | None = None,
	changelevel2: str | None = None,
	landmark2: str | None = None,
	changelevel3: str | None = None,
	landmark3: str | None = None,
	leanpvs: bool = False,
	fullphysics: bool = False,
	tas: str | None = None,
	dumpframes: bool = False,
) -> Path:
	map_name = smoke_map if smoke_map.endswith(".bsp") else f"{smoke_map}.bsp"
	map_relative = f"maps/{map_name}"
	map_source = source / map_relative
	if not map_source.is_file():
		raise FileNotFoundError(f"smoke map does not exist: {map_source}")
	destination_map_relative = None
	if changelevel:
		destination_map_name = changelevel if changelevel.endswith(".bsp") else f"{changelevel}.bsp"
		destination_map_relative = f"maps/{destination_map_name}"
		if not (source / destination_map_relative).is_file():
			raise FileNotFoundError(f"changelevel destination map does not exist: {source / destination_map_relative}")
	destination2_map_relative = None
	if changelevel2:
		destination2_map_name = changelevel2 if changelevel2.endswith(".bsp") else f"{changelevel2}.bsp"
		destination2_map_relative = f"maps/{destination2_map_name}"
		if not (source / destination2_map_relative).is_file():
			raise FileNotFoundError(
				f"changelevel2 destination map does not exist: {source / destination2_map_relative}"
			)
	destination3_map_relative = None
	if changelevel3:
		destination3_map_name = changelevel3 if changelevel3.endswith(".bsp") else f"{changelevel3}.bsp"
		destination3_map_relative = f"maps/{destination3_map_name}"
		if not (source / destination3_map_relative).is_file():
			raise FileNotFoundError(
				f"changelevel3 destination map does not exist: {source / destination3_map_relative}"
			)

	output.mkdir(parents=True, exist_ok=True)
	for relative in SMOKE_CONFIG_FILES:
		copy_if_present(source, output, relative)
	# G296: keep gfx.wad on the smoke ISO so build_iso9660 can extract the
	# retail CREDITSFONT into the tiny bootstrap font. .wad is excluded from
	# the bootstrap archive, so this does not consume the runtime preload pool.
	copy_if_present(source, output, "gfx.wad")
	if destination_map_relative:
		copy_if_present(source, output, destination_map_relative)
	if destination2_map_relative:
		copy_if_present(source, output, destination2_map_relative)
	if destination3_map_relative:
		copy_if_present(source, output, destination3_map_relative)
	for relative in MENU_RESOURCE_ASSETS:
		copy_if_present(source, output, relative)
	for relative in MENU_RESOURCE_DIRS:
		copy_tree_if_present(source, output, relative)
	# The fallback GameCube menu consumes the baked gc_menu atlas/background,
	# not only the original Steam menu resources. Smoke menu probes must stage
	# the same derived assets as retail discs or New Game stalls on blank UI.
	if not stage_gc_menu_assets(source, output):
		print("Warning: GameCube smoke menu background bake failed.", file=sys.stderr)
	write_smoke_overrides(
		output,
		smoke_map,
		newgame=newgame,
		newsaveload=newsaveload,
		configroundtrip=configroundtrip,
		saveinterrupt=saveinterrupt,
		video_mode=video_mode,
		world_render=world_render,
		phasetest=phasetest,
		changelevel=changelevel,
		landmark=landmark,
		changelevel2=changelevel2,
		landmark2=landmark2,
		changelevel3=changelevel3,
		landmark3=landmark3,
		leanpvs=leanpvs,
		fullphysics=fullphysics,
		tas=tas,
		dumpframes=dumpframes,
	)
	if tas:
		stage_probe_tas(output, tas)
	for relative in smoke_hud_resources(source):
		copy_if_present(source, output, relative)
	for relative in SMOKE_PRECACHE_MODELS:
		copy_if_present(source, output, relative)
	for relative in SMOKE_SOUND_DIRS:
		copy_tree_if_present(source, output, relative)
	# G278/G91 lean New Game: shallow media/ VO + button10 SFX. Retail overlays
	# already mirror these, but smoke ISOs previously only had deep sound/tride
	# paths that FS_OpenStream("media/…") cannot see — lean probes stayed silent.
	if newgame:
		for dest_rel, src_rel in (
			("media/c0a0_tr_gmorn.wav", "sound/tride/c0a0_tr_gmorn.wav"),
			("media/c0a0_tr_time.wav", "sound/tride/c0a0_tr_time.wav"),
			("media/ttrain1.wav", "sound/plats/ttrain1.wav"),
			("sound/buttons/button10.wav", "sound/buttons/button10.wav"),
		):
			src = source / src_rel
			if not src.is_file():
				continue
			dest = output / dest_rel
			dest.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(src, dest)
	for source_relative, alias_relative in SMOKE_CASE_ALIASES:
		copy_alias_if_present(output, source_relative, alias_relative)
	extract_wad_lump(source / "gfx.wad", output, "conchars", "gfx/conchars")

	events = source / "events"
	if events.is_dir():
		for event in sorted(events.glob("*.sc")):
			copy_if_present(source, output, f"events/{event.name}")

	copy_if_present(source, output, map_relative)
	resources = smoke_map_resources(map_source)
	for resource in sorted(resources):
		copy_if_present(source, output, resource)

	if changelevel:
		dest_stem = Path(changelevel).stem
		dest_name = f"{dest_stem}.bsp"
		dest_relative = f"maps/{dest_name}"
		dest_source = source / dest_relative
		if not dest_source.is_file():
			raise FileNotFoundError(f"changelevel map does not exist: {dest_source}")
		copy_if_present(source, output, dest_relative)
		for resource in sorted(smoke_map_resources(dest_source)):
			copy_if_present(source, output, resource)

	# Keep smoke probes on the same ARAM-backed world-texture path as retail.
	# Without these derived packs, embedded BSP textures fall back to MEM1
	# uploads and produce misleading transition-pressure failures.
	pack_stems = {Path(smoke_map).stem.lower()}
	if changelevel:
		pack_stems.add(Path(changelevel).stem.lower())
	if changelevel2:
		pack_stems.add(Path(changelevel2).stem.lower())
	build_world_texture_packs(
		source,
		output,
		Path(__file__).resolve().parents[1] / "build" / "gc-wt-cache",
		pack_stems,
	)
	from bake_capfaces_pack import build_capfaces_packs  # type: ignore

	cf_overlays = build_capfaces_packs(
		source,
		output,
		Path(__file__).resolve().parents[1] / "build" / "gc-cf-cache",
		pack_stems,
	)
	missing_cf = [s for s in pack_stems if not (output / f"media/cf_{s}.gcp").is_file()]
	if missing_cf:
		raise FileNotFoundError(
			"CapFaces pack missing for smoke map(s): " + ", ".join(sorted(missing_cf))
		)
	del cf_overlays

	# Full BSP PVS cluster display lists (preferred world path over CapFaces).
	media_out = output / "media"
	media_out.mkdir(parents=True, exist_ok=True)
	for stem in sorted(pack_stems):
		name = f"wd_{stem}.gcd"
		dest = media_out / name
		src = None
		repo_root = Path(__file__).resolve().parents[3]  # .../dod-gc
		for cand in (
			source / "media" / name,
			repo_root / "content" / "dod" / "media" / name,
		):
			if cand.is_file():
				src = cand
				break
		if src is None:
			print(f"  world DL: no {name} (CapFaces/live BSP fallback)")
			continue
		shutil.copy2(src, dest)
		print(f"  world DL: staged {name} ({dest.stat().st_size} bytes)")

	(output / "custom").mkdir(exist_ok=True)
	(output / "maps").mkdir(exist_ok=True)
	return output


def stage_intro_avi_data(source: Path, output: Path) -> Path:
	output.mkdir(parents=True, exist_ok=True)
	for relative in CRITICAL_ASSETS:
		copy_if_present(source, output, relative)
	for relative in SMOKE_CONFIG_FILES:
		copy_if_present(source, output, relative)
	for relative in MENU_RESOURCE_ASSETS:
		copy_if_present(source, output, relative)
	for relative in MENU_RESOURCE_DIRS:
		copy_tree_if_present(source, output, relative)
	(output / "valve.rc").write_text("stuffcmds\n", encoding="ascii")
	(output / "config.cfg").write_text("\n", encoding="ascii")
	(output / "autoexec.cfg").write_text("\n", encoding="ascii")
	(output / "mainui.cfg").write_text("\n", encoding="ascii")
	extract_wad_lump(source / "gfx.wad", output, "conchars", "gfx/conchars")
	for relative in SMOKE_INTRO_MEDIA:
		copy_if_present(source, output, relative)
	build_intro_gcvid_companions(output)
	write_startup_vids(output)
	if not stage_gc_menu_assets(source, output):
		print("Warning: GameCube retail menu background bake failed.", file=sys.stderr)
	if not stage_gc_loading_assets(source, output):
		print("Warning: GameCube loading plaque bake failed.", file=sys.stderr)
	(output / "custom").mkdir(exist_ok=True)
	return output


def build_disc(
	dol: Path,
	data: Path,
	extras: Path | None,
	output_path: Path,
	apploader_source: Path,
	apploader_linker: Path,
	bootstrap_recursive: bool = False,
	overlays: tuple[tuple[str, Path], ...] = (),
	gamedir: str = "valve",
	extra_gamedirs: tuple[tuple[str, Path], ...] = (),
) -> None:
	output_path.parent.mkdir(parents=True, exist_ok=True)
	build_iso9660(
		data,
		extras,
		output_path,
		bootstrap_recursive,
		overlays,
		gamedir=gamedir,
		extra_gamedirs=extra_gamedirs,
	)
	iso9660_size = output_path.stat().st_size
	dol_size = dol.stat().st_size
	dol_offset = align(iso9660_size, 0x800)
	# The boot process requires an FST, while game data is deliberately exposed
	# through ISO9660 so libc and filesystem_stdio can use normal POSIX calls.
	# Build FST from staged data tree
	root = Node("xash3d")
	add_tree(root, data)
	entries: list[Node] = []
	flatten(root, 0, entries)
	names = encode_names(entries)
	# Assign disc offsets for files (after ISO9660 data)
	file_offset = align(DISC_HEADER_SIZE + dol_size, 0x800)
	for entry in entries:
		if not entry.is_dir:
			entry.disc_offset = file_offset
			file_offset += align(entry.source.stat().st_size, 0x20)
	fst = build_fst(entries, names)
	fst_offset = align(dol_offset + dol_size, 0x20)
	fst_size = len(fst)
	next_offset = align(fst_offset + fst_size, 0x800)
	apploader = build_apploader(
		apploader_source, apploader_linker, dol, dol_offset, fst_offset, fst_size
	)
	header = bytearray(DISC_HEADER_SIZE)
	title = b"Xash3D GameCube Test"
	header[0:6] = b"GXHE00"
	# Keep the fixed-size disc header fixed. Assigning a shorter value to a
	# longer bytearray slice changes its length and shifts every following byte.
	header[0x20:0x20 + len(title)] = title
	struct.pack_into(">I", header, 0x1C, DISC_MAGIC)
	struct.pack_into(">I", header, 0x420, dol_offset)
	struct.pack_into(">I", header, 0x424, fst_offset)
	struct.pack_into(">I", header, 0x428, fst_size)
	struct.pack_into(">I", header, 0x42C, fst_size)
	struct.pack_into(">I", header, 0x430, 0x8000)
	struct.pack_into(">I", header, 0x434, iso9660_size - 0x8000)
	struct.pack_into(">I", header, 0x458, 1)  # NTSC region in BI2
	# Apploader header at disc offset 0x2440 (BI2 header area)
	# Entry point and length are written at 0x2450 and 0x2454
	struct.pack_into(">I", header, 0x2450, APPLOADER_ADDRESS)
	struct.pack_into(">I", header, 0x2454, len(apploader))
	header[APPLOADER_DATA_OFFSET:APPLOADER_DATA_OFFSET + len(apploader)] = apploader
	if len(header) != DISC_HEADER_SIZE:
		raise AssertionError(
			f"disc header changed size: {len(header):#x} != {DISC_HEADER_SIZE:#x}"
		)

	with output_path.open("r+b") as output:
		output.write(header)
		# Preserve the ISO9660 descriptors and file data between the GameCube
		# system area and the appended executable.
		output.seek(dol_offset)
		with dol.open("rb") as source:
			shutil.copyfileobj(source, output)
		write_padding(output, fst_offset)
		output.write(fst)
		write_padding(output, next_offset)

	# The apploader reads DOL sections using offsets relative to DOL_OFFSET.
	# Verify the complete embedded DOL so layout regressions fail during build
	# instead of surfacing as an invalid PowerPC instruction in Dolphin.
	with output_path.open("rb") as output, dol.open("rb") as source:
		output.seek(dol_offset)
		embedded_dol = output.read(dol_size)
		expected_dol = source.read()
	if embedded_dol != expected_dol:
		raise ValueError("embedded DOL does not match the input DOL")

	print(f"Built {output_path} ({next_offset} bytes, hybrid GameCube/ISO9660)")
	handoff = output_path.parent / f"{output_path.stem}-handoff.txt"
	# Reject objcopy pseudo-DOL: real DOLs start with text section offsets.
	with dol.open("rb") as df:
		dol_hdr = df.read(0x100)
	if len(dol_hdr) < 0x100 or dol_hdr[0:4] == b"\x7fELF":
		raise ValueError(
			f"boot.dol looks invalid (elf2dol required, not objcopy): {dol}"
		)
	handoff.write_text(
		"\n".join(
			[
				"xash3d-gc disc hardware handoff",
				f"iso={output_path}",
				f"iso_bytes={next_offset}",
				f"dol={dol}",
				f"dol_bytes={dol_size}",
				"renderer=REF_GX",
				"policy=retail-flipper",
				"note=boot.dol must be produced by elf2dol (objcopy pseudo-DOL rejected)",
			]
		)
		+ "\n",
		encoding="utf-8",
	)
	print(f"Handoff metadata: {handoff}")


def validate_assets(data_path: Path) -> list[str]:
	"""
	Validate that a retail source tree contains the files needed for staging.

	Checks the user's original Half-Life install only. Format, size, and case
	normalization happen during stage_retail_data() without editing source files.
	"""
	errors = []

	if not data_path.is_dir():
		return [f"Data directory not found: {data_path}"]

	for asset in CRITICAL_ASSETS:
		asset_path = data_path / asset
		if not asset_path.exists():
			errors.append(f"MISSING: Critical asset '{asset}' is not present.")
		elif not asset_path.is_file():
			errors.append(f"ERROR: Critical path '{asset}' exists but is not a file.")

	return errors


def validate_staged_retail_assets(data_path: Path) -> list[str]:
	"""Validate the staged disc payload after retail normalization."""
	errors = []

	if not data_path.is_dir():
		return [f"Data directory not found: {data_path}"]

	for asset in CRITICAL_ASSETS:
		asset_path = data_path / asset
		if not asset_path.is_file():
			errors.append(f"MISSING: Staged critical asset '{asset}' is not present.")

	for root, _dirs, files in os.walk(data_path):
		rel_root = os.path.relpath(root, data_path)
		if rel_root == ".":
			rel_root = ""

		for filename in files:
			filepath = data_path / rel_root / filename
			rel_path = os.path.join(rel_root, filename).replace("\\", "/")
			_, ext = os.path.splitext(filename)

			if in_strict_case_dir(rel_root) and filename != filename.lower():
				errors.append(
					f"CASE_MISMATCH: '{rel_path}' is still mixed case after staging."
				)

			if ext.lower() in UNSUPPORTED_EXTENSIONS:
				errors.append(
					f"UNSUPPORTED: '{rel_path}' has extension '{ext}' which is not supported on GameCube."
				)

			try:
				size = filepath.stat().st_size
				if size > MAX_ASSET_SIZE and ext.lower() not in {".bsp", ".wad", ".pak", ".pk3"}:
					errors.append(
						f"OVERSIZED: '{rel_path}' is {size} bytes. "
						f"Limit is {MAX_ASSET_SIZE} bytes outside pack archives."
					)
			except OSError:
				pass

	return errors


def validate_smoke_assets(data_path: Path, smoke_map: str) -> list[str]:
	"""
	Validate the already-staged smoke-test subset.

	This is intentionally narrower than validate_assets(): a smoke package may
	contain map-referenced WAD names and known mixed-case Half-Life model aliases
	while still being safe for the bounded GameCube boot probes.
	"""
	errors = []
	allowed_mixed_case = {alias for _, alias in SMOKE_CASE_ALIASES}
	required = (
		"liblist.gam",
		"default.cfg",
		"config.cfg",
		"valve.rc",
		"gfx/palette.lmp",
		"gfx/colormap.lmp",
		"gfx/conchars",
		f"maps/{Path(smoke_map).stem}.bsp",
		# CapFaces offline pack required for Flipper 60 FPS path.
		f"media/cf_{Path(smoke_map).stem.lower()}.gcp",
	)

	if not data_path.is_dir():
		return [f"Data directory not found: {data_path}"]

	for relative in required:
		path = data_path / relative
		if not path.is_file():
			errors.append(f"MISSING: Staged smoke asset '{relative}' is not present.")

	for root, _dirs, files in os.walk(data_path):
		rel_root = os.path.relpath(root, data_path)
		in_strict_dir = any(rel_root.startswith(d) or rel_root == d for d in STRICT_CASE_DIRS)

		for filename in files:
			filepath = data_path / rel_root / filename
			rel_path = os.path.join(rel_root, filename).replace("\\", "/")
			_, ext = os.path.splitext(filename)

			if in_strict_dir and filename != filename.lower() and rel_path not in allowed_mixed_case:
				errors.append(f"CASE_MISMATCH: '{rel_path}' contains uppercase characters without a known smoke alias.")

			if ext.lower() in UNSUPPORTED_EXTENSIONS:
				errors.append(f"UNSUPPORTED: '{rel_path}' has extension '{ext}' which is not supported on GameCube.")

			try:
				size = filepath.stat().st_size
				if size > MAX_ASSET_SIZE and ext.lower() not in {".bsp", ".wad"}:
					errors.append(f"OVERSIZED: '{rel_path}' is {size} bytes. "
					              f"Limit is {MAX_ASSET_SIZE} bytes outside BSP/WAD smoke dependencies.")
			except OSError:
				pass

	return errors

# Files the pre-2026-10 SD stager wrote that the engine would still mount
# (every *.pk3 in valve/ is a search path; each costs FS pool memory on MEM1).
LEGACY_SD_STAGE_PK3 = ("models.pk3", "sound.pk3", "materials.pk3", "maps.pk3", "scripts.pk3", "cfg.pk3")

# Default gamecube.cfg for the Swiss SD route: the Dolphin-tested menu New
# Game memory policy (-gcmenuplaystart) with the player's real controller.
SD_DEFAULT_BOOT_CFG = ("retailnewgame",)


def stage_sd_card(
	source: Path,
	card_root: Path,
	dol: Path | None,
	extras: Path | None,
	*,
	include_startup_vids: bool = True,
	boot_cfg: tuple[str, ...] = SD_DEFAULT_BOOT_CFG,
) -> Path:
	"""Stage the retail disc payload onto a Swiss FAT volume (sd:/, carda:/, cardb:/).

	The layout under <card>/xash3d/valve matches what build_disc() puts under
	/xash3d/valve on the ISO that Dolphin probes boot, so the hardware SD route
	runs the same files. Source files are never modified. Existing saves and a
	player's config.cfg on the card are kept.
	"""
	valve = card_root / "xash3d" / "valve"
	for sub in ("save", "logs", "screenshots"):
		(valve / sub).mkdir(parents=True, exist_ok=True)

	for name in LEGACY_SD_STAGE_PK3:
		legacy = valve / name
		if legacy.is_file():
			legacy.unlink()
			print(f"Removed legacy SD stage archive {legacy.relative_to(card_root)}")

	player_config = (valve / "config.cfg").is_file()
	_, skipped = stage_retail_data(source, valve)
	if skipped:
		print(
			f"Retail staging: omitted {skipped} unsupported file(s) "
			f"({', '.join(sorted(UNSUPPORTED_EXTENSIONS))})."
		)
	if not stage_gc_menu_assets(source, valve):
		print("Warning: GameCube retail menu background bake failed.", file=sys.stderr)
	if not stage_gc_loading_assets(source, valve):
		print("Warning: GameCube loading plaque bake failed.", file=sys.stderr)

	with tempfile.TemporaryDirectory(prefix="xash3d-gc-sd-overlay-") as temp:
		overlays = create_startup_vids_overlay(
			source, Path(temp) / "valve", include_startup_vids=include_startup_vids
		)
		for relative, overlay in overlays:
			dest = valve / relative
			# The disc ships an empty config.cfg. On SD a config.cfg that was
			# already on the card holds the player's saved options; keep it.
			if relative == "config.cfg" and player_config:
				continue
			dest.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(overlay, dest)

	cfg = valve / "gamecube.cfg"
	if boot_cfg:
		cfg.write_text("\n".join(boot_cfg) + "\n", encoding="ascii")
		print(f"Wrote {cfg.relative_to(card_root)}: {', '.join(boot_cfg)}")
	elif cfg.is_file():
		cfg.unlink()
		print(f"Removed {cfg.relative_to(card_root)} (plain retail menu boot)")

	# Same archive build_iso9660() grafts onto the disc (it skips *.pk3, so a
	# re-stage does not pack the previous copy).
	with tempfile.TemporaryDirectory(prefix="xash3d-gc-sd-bootstrap-") as temp:
		bootstrap = Path(temp) / "gamecube-bootstrap.pk3"
		write_bootstrap_pk3(valve, bootstrap)
		shutil.copy2(bootstrap, valve / "gamecube-bootstrap.pk3")
	if extras is not None:
		shutil.copy2(extras, valve / "extras.pk3")

	if dol is not None:
		app = card_root / "apps" / "xash3d-gc"
		app.mkdir(parents=True, exist_ok=True)
		# Always publish boot.dol for Swiss layouts that expect the legacy name.
		shutil.copy2(dol, app / "boot.dol")
		# Also keep the uniquely named release DOL when the source is named.
		if dol.name not in ("boot.dol", "boot.DOL"):
			shutil.copy2(dol, app / dol.name)

	return valve


def main() -> None:
	print(GC_BUILD_BANNER)
	print("Xash3D GameCube: content build begins")
	script_dir = Path(__file__).resolve().parent
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--dol", type=Path, default=Path("OUT/bin/boot.dol"))
	parser.add_argument("--data", type=Path, default=Path("Half-Life/valve"))
	parser.add_argument(
		"--gamedir",
		default="valve",
		help="primary ISO/SD folder under /xash3d/ (default: valve → linked HL1 DOL)",
	)
	parser.add_argument(
		"--extra-data",
		action="append",
		default=[],
		metavar="GAMEDIR=PATH",
		help="additional /xash3d/<GAMEDIR> graft (repeatable); e.g. dod=Half-Life/dod",
	)
	parser.add_argument(
		"--no-auto-extra-gamedirs",
		action="store_true",
		help="do not auto-stage sibling gamedirs next to --data (dod, gearbox, …)",
	)
	parser.add_argument("--extras", type=Path, default=Path("OUT/valve/extras.pk3"))
	parser.add_argument("--output", type=Path, default=Path("OUT/xash3d-gc.iso"))
	parser.add_argument(
		"--smoke-map",
		metavar="MAP",
		help="stage only the files needed for a bounded legal local map smoke test",
	)
	parser.add_argument(
		"--world-render",
		action="store_true",
		help="enable gcmap world render probe via gamecube.cfg gcworldrender override",
	)
	parser.add_argument(
		"--intro-avi",
		action="store_true",
		help="stage boot essentials and original user-provided startup AVI files for local intro testing",
	)
	parser.add_argument(
		"--probe-newgame",
		action="store_true",
		help="stage valve/gamecube.cfg with a newgame override for automated retail probes",
	)
	parser.add_argument(
		"--play",
		action="store_true",
		help="stage gamecube.cfg retailnewgame: the menu New Game route with the real controller (for playing in Dolphin)",
	)
	parser.add_argument(
		"--perf",
		action="store_true",
		help="enable GameCube performance overlay/profiling in the interactive play config",
	)
	parser.add_argument(
		"--probe-menu-newgame",
		action="store_true",
		help="stage a retail menu boot that presses the built-in New Game item",
	)
	parser.add_argument(
		"--probe-newsaveload",
		action="store_true",
		help="with --probe-newgame, also stage newsaveload for G94 RAM save/load probes",
	)
	parser.add_argument(
		"--probe-configroundtrip",
		action="store_true",
		help="stage configroundtrip for G508 Dolphin-designated config write/read probes",
	)
	parser.add_argument(
		"--probe-saveinterrupt",
		action="store_true",
		help="with configroundtrip, inject one *.new rename fault for SAVE-022 recovery",
	)
	parser.add_argument(
		"--probe-video-mode",
		metavar="MODE",
		choices=("ntsc", "prog", "pal"),
		help="stage gamecube.cfg video <MODE> for VIDEO-022 NTSC/PAL/progressive probes",
	)
	parser.add_argument(
		"--probe-phasetest",
		metavar="PHASE",
		help="stage gamecube.cfg phasetest <PHASE> for G82 intentional boot-phase fault smoke",
	)
	parser.add_argument(
		"--probe-changelevel",
		metavar="MAP",
		help="stage gamecube.cfg changelevel <MAP> for G68 transition probes (with --smoke-map or --probe-newgame)",
	)
	parser.add_argument(
		"--probe-landmark",
		metavar="NAME",
		help="with --probe-changelevel, stage landmark <NAME> for G97 smooth hop probes",
	)
	parser.add_argument(
		"--probe-changelevel2",
		metavar="MAP",
		help="G356: stage gamecube.cfg changelevel2 <MAP> for dual-hop campaign probes",
	)
	parser.add_argument(
		"--probe-landmark2",
		metavar="NAME",
		help="with --probe-changelevel2, stage landmark2 <NAME>",
	)
	parser.add_argument(
		"--probe-changelevel3",
		metavar="MAP",
		help="G357: stage gamecube.cfg changelevel3 <MAP> for triple-hop campaign probes",
	)
	parser.add_argument(
		"--probe-landmark3",
		metavar="NAME",
		help="with --probe-changelevel3, stage landmark3 <NAME>",
	)
	parser.add_argument(
		"--probe-leanpvs",
		action="store_true",
		help="stage gamecube.cfg leanpvs to force G101 lean-N FatPVS (skip full multi-cluster)",
	)
	parser.add_argument(
		"--probe-fullphysics",
		action="store_true",
		help="with --probe-newgame, bypass bounded server substitutes and run normal Xash3D physics",
	)
	parser.add_argument(
		"--probe-tas",
		metavar="NAME",
		help="stage valve/tas/<NAME>.tas and gamecube.cfg tas <NAME> for pad TAS replay probes",
	)
	parser.add_argument(
		"--probe-dumpframes",
		action="store_true",
		help="stage gamecube.cfg dumpframes (-gcdumpframes) for soft DumpFrames PNG evidence",
	)
	parser.add_argument(
		"--skip-startup-vids",
		action="store_true",
		help="overlay an empty media/StartupVids.txt for faster retail menu boot validation",
	)
	parser.add_argument(
		"--apploader-source", type=Path, default=script_dir / "gamecube-apploader.c"
	)
	parser.add_argument(
		"--apploader-linker", type=Path, default=script_dir / "gamecube-apploader.ld"
	)
	args = parser.parse_args()

	for path in (args.dol, args.data):
		if not path.exists():
			parser.error(f"required path does not exist: {path}")
	# Never package a stale DOL when a newer GameCube ELF is present.  This is
	# especially important for runtime probes: a clean host build must be the
	# artifact that Dolphin actually boots.
	elf_candidate = Path("build/engine/xash")
	if elf_candidate.is_file() and args.dol.is_file() and elf_candidate.stat().st_mtime > args.dol.stat().st_mtime:
		parser.error(
			f"stale DOL: {args.dol} predates {elf_candidate}; run scripts/build-gamecube.sh first"
		)
			
	if args.smoke_map and args.intro_avi:
		parser.error("--smoke-map and --intro-avi are mutually exclusive")
	if args.probe_newsaveload and not (args.probe_newgame or args.smoke_map):
		parser.error("--probe-newsaveload requires --probe-newgame or --smoke-map")
	if args.probe_configroundtrip and not (args.probe_newgame or args.smoke_map):
		parser.error("--probe-configroundtrip requires --probe-newgame or --smoke-map")
	if args.probe_saveinterrupt and not (args.probe_configroundtrip or args.probe_newgame or args.smoke_map):
		parser.error("--probe-saveinterrupt requires --probe-configroundtrip (or New Game/smoke map staging)")
	listen_boot = False
	if args.data:
		boot_cfg = Path(args.data) / "gamecube.cfg"
		if boot_cfg.is_file():
			listen_boot = any(
				line.strip().lower().startswith(("listenserver ", "connect "))
				for line in boot_cfg.read_text(encoding="ascii", errors="ignore").splitlines()
			)
	if args.world_render and not (args.smoke_map or args.play or listen_boot):
		parser.error("--world-render requires --smoke-map, --play, or a listen/connect boot config")
	if args.probe_phasetest:
		phase = args.probe_phasetest.strip().lower()
		valid = {"early", "engine", "renderer", "sw_fb", "menu", "client", "intro", "map"}
		if phase not in valid:
			parser.error(f"--probe-phasetest must be one of: {', '.join(sorted(valid))}")
		args.probe_phasetest = phase
	if args.probe_tas:
		tas_name = args.probe_tas.strip()
		if not tas_name or any(c in tas_name for c in "/\\."):
			parser.error("--probe-tas name must be a simple token (no path or extension)")
		tas_src = script_dir / "tas" / f"{tas_name}.tas"
		if not tas_src.is_file():
			parser.error(f"--probe-tas script missing: {tas_src}")
		args.probe_tas = tas_name

	try:
		primary_gamedir = _safe_gamedir_name(args.gamedir)
	except ValueError as exc:
		parser.error(str(exc))

	try:
		extra_gamedirs = parse_extra_data_args(args.extra_data)
	except (ValueError, FileNotFoundError) as exc:
		parser.error(str(exc))

	if not args.no_auto_extra_gamedirs:
		auto_extras = discover_sibling_gamedirs(args.data, primary_gamedir)
		explicit = {name for name, _ in extra_gamedirs}
		for name, path in auto_extras:
			if name in explicit or name == primary_gamedir:
				continue
			print(f"GameCube ISO: auto-staging sibling gamedir {name} from {path}")
			extra_gamedirs.append((name, path))
	extra_gamedirs_tuple = tuple(extra_gamedirs)

	# Full retail builds validate source, then stage a normalized copy for the ISO.
	# HL1 uses the strict critical list; other titles only require marker files
	# (DoD / OpFor trees often omit valve.rc and HL campaign lumps).
	if not args.smoke_map and not args.intro_avi:
		if primary_gamedir == "valve":
			validation_errors = validate_assets(args.data)
		else:
			validation_errors = []
			if not args.data.is_dir():
				validation_errors.append(f"Data directory not found: {args.data}")
			else:
				has_marker = (args.data / "liblist.gam").is_file() or (args.data / "gfx.wad").is_file()
				if not has_marker:
					validation_errors.append(
						f"MISSING: {primary_gamedir} needs liblist.gam or gfx.wad under {args.data}"
					)
		if validation_errors:
			print("Asset validation failed:", file=sys.stderr)
			for error in validation_errors:
				print(f"  - {error}", file=sys.stderr)
			sys.exit(1)

	extras = args.extras if args.extras.exists() else None
	if args.smoke_map:
		with tempfile.TemporaryDirectory(prefix="xash3d-gc-smoke-data-") as temp:
			smoke_data = stage_smoke_data(
				args.data,
				Path(temp) / primary_gamedir,
				args.smoke_map,
				# ISO boots rebuild argv from gamecube.cfg; Dolphin -- guest
				# args never reach the DOL. Bake newgame whenever the probe
				# asks for it (lean New Game or fullphysics/save/config).
				newgame=(
					args.probe_newgame
					or args.probe_fullphysics
					or args.probe_newsaveload
					or args.probe_configroundtrip
					or args.probe_saveinterrupt
				),
				newsaveload=args.probe_newsaveload,
				configroundtrip=args.probe_configroundtrip or args.probe_saveinterrupt,
				saveinterrupt=args.probe_saveinterrupt,
				video_mode=args.probe_video_mode,
				world_render=args.world_render,
				phasetest=args.probe_phasetest,
				changelevel=args.probe_changelevel,
				landmark=args.probe_landmark,
				changelevel2=args.probe_changelevel2,
				landmark2=args.probe_landmark2,
				changelevel3=args.probe_changelevel3,
				landmark3=args.probe_landmark3,
				leanpvs=args.probe_leanpvs,
				fullphysics=args.probe_fullphysics,
				tas=args.probe_tas,
				dumpframes=args.probe_dumpframes,
			)
			validation_errors = validate_smoke_assets(smoke_data, args.smoke_map)
			if validation_errors:
				print("Smoke asset validation failed:", file=sys.stderr)
				for error in validation_errors:
					print(f"  - {error}", file=sys.stderr)
				sys.exit(1)
			if args.probe_menu_newgame:
				# Lean menu→New Game: keep smoke assets, but boot the menu and
				# press New Game. Do NOT stage `map <smoke>` — that becomes
				# -gcmap and auto-loads, bypassing the menu path entirely
				# (probe 20261104-042155). GI->startmap still defaults to c0a0;
				# stage the smoke BSP under that name when needed.
				menu_lines = ["menunewgame"]
				if args.probe_fullphysics:
					menu_lines.append("fullphysics")
				if args.probe_dumpframes:
					menu_lines.append("dumpframes")
				if os.environ.get("XASH_GC_PROBE_MEMREPORT") == "1":
					menu_lines.append("memreport")
				if os.environ.get("XASH_GC_PROBE_NOTEXPACKS") == "1":
					menu_lines.append("notexpacks")
				(smoke_data / "gamecube.cfg").write_text(
					"\n".join(menu_lines) + "\n", encoding="ascii"
				)
				print(
					f"Smoke menu New Game: overridden gamecube.cfg "
					f"({', '.join(menu_lines)}) for map {args.smoke_map}"
				)
			build_disc(
				args.dol,
				smoke_data,
				extras,
				args.output,
				args.apploader_source,
				args.apploader_linker,
				bootstrap_recursive=True,
				gamedir=primary_gamedir,
				extra_gamedirs=extra_gamedirs_tuple,
			)
	elif args.intro_avi:
		with tempfile.TemporaryDirectory(prefix="xash3d-gc-intro-avi-data-") as temp:
			intro_data = stage_intro_avi_data(args.data, Path(temp) / primary_gamedir)
			staged_movies = [
				relative for relative in SMOKE_INTRO_MEDIA
				if (intro_data / relative).is_file()
			]
			if not staged_movies:
				print(
					"Intro AVI staging failed: no original local startup AVI files were found.",
					file=sys.stderr,
				)
				sys.exit(1)
			build_disc(
				args.dol,
				intro_data,
				extras,
				args.output,
				args.apploader_source,
				args.apploader_linker,
				bootstrap_recursive=True,
				gamedir=primary_gamedir,
				extra_gamedirs=extra_gamedirs_tuple,
			)
	else:
		print(
			f"Staging retail assets for GameCube gamedir={primary_gamedir} "
			"(source files are not modified)."
		)
		with tempfile.TemporaryDirectory(prefix="xash3d-gc-retail-data-") as temp:
			staged_root = Path(temp) / primary_gamedir
			staged_data, skipped = stage_retail_data(args.data, staged_root)
			if skipped:
				print(
					f"Retail staging: omitted {skipped} unsupported file(s) "
					f"({', '.join(sorted(UNSUPPORTED_EXTENSIONS))})."
				)
			if not stage_gc_menu_assets(args.data, staged_data):
				print("Warning: GameCube retail menu background bake failed.", file=sys.stderr)
			if not stage_gc_loading_assets(args.data, staged_data):
				print("Warning: GameCube loading plaque bake failed.", file=sys.stderr)
			if primary_gamedir == "valve":
				validation_errors = validate_staged_retail_assets(staged_data)
				if validation_errors:
					print("Staged retail asset validation failed:", file=sys.stderr)
					for error in validation_errors:
						print(f"  - {error}", file=sys.stderr)
					sys.exit(1)

			if args.probe_newgame:
				write_probe_newgame_override(
					staged_data,
					newsaveload=args.probe_newsaveload,
					configroundtrip=args.probe_configroundtrip or args.probe_saveinterrupt,
					saveinterrupt=args.probe_saveinterrupt,
					video_mode=args.probe_video_mode,
					phasetest=args.probe_phasetest,
					changelevel=args.probe_changelevel,
					landmark=args.probe_landmark,
					changelevel2=args.probe_changelevel2,
					landmark2=args.probe_landmark2,
					changelevel3=args.probe_changelevel3,
					landmark3=args.probe_landmark3,
					# G68/G100: start map for -gcmap early changelevel + landmark plant.
					smoke_map="c0a0" if args.probe_changelevel else None,
					leanpvs=args.probe_leanpvs,
					fullphysics=args.probe_fullphysics,
					tas=args.probe_tas,
					dumpframes=args.probe_dumpframes,
				)
				if args.probe_tas:
					stage_probe_tas(staged_data, args.probe_tas, scripts_root=script_dir)
			elif args.play:
				# Interactive play must enter the retail intro and menu before it
				# starts a server.  retailnewgame preloads c0a0 for an automated
				# menu→New Game route; that leaves too little MEM1 for the menu's
				# baked background and presents only its solid-colour fallback.
				# Normal New Game from MainUI still uses the full physics path.
				play_lines = []
				# Prefer progressive so menu/HUD CopyDisp skips CRT vfilter
				# (VI_PROGRESSIVE was misclassified as interlaced before).
				play_lines.append(
					f"video {args.probe_video_mode}" if args.probe_video_mode else "video prog"
				)
				if args.world_render:
					play_lines.append("gcworldrender")
				if args.perf:
					play_lines.extend(["profile", "gc_perf_enable 1", "gc_perf_draw 1"])
				if os.environ.get("XASH_GC_PROBE_AUTOHOP") == "1":
					play_lines.append("autohop")
				if os.environ.get("XASH_GC_PROBE_MODELTRACE") == "1":
					play_lines.append("modeltrace")
				if os.environ.get("XASH_GC_PROBE_PROFILE") == "1":
					play_lines.append("profile")
				# Early BBA/eth2sp1 via gamecube.cfg → argv -net*.
				if os.environ.get("XASH_GC_PROBE_NET", "").lower() in ("1", "yes", "true", "on"):
					# Dolphin BuiltIn: static IP avoids DHCP hangs; netfast caps retries.
					mode = (os.environ.get("XASH_GC_PROBE_NET_MODE") or "netstatic").strip().lower()
					if mode not in ("net", "netfast", "netstatic"):
						mode = "netstatic"
					play_lines.append(mode)
				(staged_data / "gamecube.cfg").write_text(
					"\n".join(play_lines) + "\n", encoding="ascii"
				)
			elif args.probe_menu_newgame:
				menu_lines = ["menunewgame"]
				if args.probe_video_mode:
					menu_lines.append(f"video {args.probe_video_mode}")
				else:
					# Match play ISO: progressive so menu/LOADING CopyDisp
					# skips the CRT vfilter path.
					menu_lines.append("video prog")
				if args.probe_fullphysics:
					menu_lines.append("fullphysics")
				if args.probe_tas:
					menu_lines.append(f"tas {args.probe_tas}")
				if args.probe_dumpframes:
					menu_lines.append("dumpframes")
				if os.environ.get("XASH_GC_PROBE_AUTOHOP") == "1":
					# Walk out of the tram on c0a0e into c1a0 (intro proof).
					menu_lines.append("autohop")
				if os.environ.get("XASH_GC_PROBE_MEMREPORT") == "1":
					menu_lines.append("memreport")
				if os.environ.get("XASH_GC_PROBE_PROFILE") == "1":
					menu_lines.append("profile")
				if os.environ.get("XASH_GC_PROBE_NOLIVEPVS") == "1":
					menu_lines.append("nolivepvs")
				if os.environ.get("XASH_GC_PROBE_NONATIVETEX") == "1":
					menu_lines.append("nonativetex")
				if os.environ.get("XASH_GC_PROBE_NOTEXPACKS") == "1":
					menu_lines.append("notexpacks")
				(staged_data / "gamecube.cfg").write_text(
					"\n".join(menu_lines) + "\n", encoding="ascii"
				)
			elif args.probe_phasetest:
				write_probe_phasetest_override(staged_data, args.probe_phasetest)
			elif args.probe_dumpframes:
				# Plain retail menu + soft DumpFrames evidence (no New Game).
				menu_lines = ["dumpframes"]
				if args.probe_video_mode:
					menu_lines.append(f"video {args.probe_video_mode}")
				(staged_data / "gamecube.cfg").write_text(
					"\n".join(menu_lines) + "\n", encoding="ascii"
				)
				print(
					"Retail menu: staged gamecube.cfg dumpframes"
					+ (f" video {args.probe_video_mode}" if args.probe_video_mode else "")
					+ " for soft DumpFrames"
				)

			overlay_root = Path(temp) / "overlay" / primary_gamedir
			overlays = create_startup_vids_overlay(
				args.data,
				overlay_root,
				include_startup_vids=not args.skip_startup_vids,
			)
			build_disc(
				args.dol,
				staged_data,
				extras,
				args.output,
				args.apploader_source,
				args.apploader_linker,
				overlays=overlays,
				gamedir=primary_gamedir,
				extra_gamedirs=extra_gamedirs_tuple,
			)


if __name__ == "__main__":
	main()
