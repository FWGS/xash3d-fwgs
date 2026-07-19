#!/usr/bin/env python3
"""Build a bootable GameCube disc image for local Xash3D testing."""

from __future__ import annotations

import argparse
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

# Critical assets required for basic engine boot and map loading
CRITICAL_ASSETS = (
    "liblist.gam",
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
BOOTSTRAP_EXCLUDED_EXTENSIONS = {".avi", ".gcvid", ".mdl", ".pak", ".pk3", ".wad", ".wav"}
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
	names = bytearray(b"\0")
	for entry in entries[1:]:
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


def build_iso9660(
	data: Path,
	extras: Path | None,
	output_path: Path,
	bootstrap_recursive: bool = False,
	overlays: tuple[tuple[str, Path], ...] = (),
) -> None:
	xorriso = shutil.which("xorriso")
	if xorriso is None:
		raise FileNotFoundError("xorriso is required to build the GameCube data disc")

	command = [
		xorriso,
		"-as", "mkisofs",
		"-quiet",
		"-iso-level", "3",
		"-J",
		"-R",
		"-V", "XASH3D_GC",
		"-o", str(output_path),
		"-graft-points",
		f"/xash3d/valve={data}",
	]
	with tempfile.TemporaryDirectory(prefix="xash3d-gc-bootstrap-") as temp:
		bootstrap = Path(temp) / "gamecube-bootstrap.pk3"
		with zipfile.ZipFile(bootstrap, "w", zipfile.ZIP_DEFLATED) as archive:
			children = data.rglob("*") if bootstrap_recursive else data.iterdir()
			for child in sorted(children):
				suffix = child.suffix.lower()
				if (
					child.is_file()
					and suffix not in BOOTSTRAP_EXCLUDED_EXTENSIONS
					and child.stat().st_size <= 2 * 1024 * 1024
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
			studio_count = inject_gc_studio_into_bootstrap(archive, data)
			if studio_count:
				print(f"GameCube studio mirror: injected {studio_count} MDL(s) into bootstrap pk3")
			hud_count = inject_gc_hud_into_bootstrap(archive, data)
			if hud_count:
				print(f"GameCube HUD mirror: injected {hud_count} sprite(s) into bootstrap pk3")
			sky_count = inject_gc_sky_into_bootstrap(archive, data)
			if sky_count:
				print(f"GameCube sky mirror: injected {sky_count} lean BMP(s) into bootstrap pk3")

		if extras is not None:
			command.append(f"/xash3d/valve/extras.pk3={extras}")
		for relative, source in overlays:
			command.append(f"/xash3d/valve/{relative}={source}")
		command.append(f"/xash3d/valve/gamecube-bootstrap.pk3={bootstrap}")
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
	"""Keep the retail menu moody and dark instead of over-brightening it."""
	from PIL import ImageEnhance

	rgba = image.convert("RGBA")
	rgb = rgba.convert("RGB")
	# Very small contrast lift so 128x96 survives RGB565 without blowing out the art.
	rgb = ImageEnhance.Contrast(rgb).enhance(1.04)
	return rgb.convert("RGBA")


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


def stage_gc_menu_assets(source: Path, output: Path) -> bool:
	"""Bake a single MEM1-friendly retail menu background and title logo."""
	try:
		from PIL import Image
	except ImportError:
		print("Warning: Pillow not installed; skipping GameCube menu background bake.", file=sys.stderr)
		return False

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

	# 128x96 keeps the transient RGBA decode buffer under 50 KiB for MEM1 menu boot.
	if best is not None and best_ratio >= 0.02:
		background = _gc_menu_preserve_retail_tone(
			best.resize((128, 96), Image.Resampling.LANCZOS))
		source_note = best_label
	else:
		background = _gc_menu_synthetic_background((128, 96))
		source_note = "synthetic fallback (retail tiles missing or pure black)"

	background_path = menu_dir / "background.tga"
	background.save(background_path, format="TGA")

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
		logo.thumbnail((256, 32), Image.Resampling.LANCZOS)
		logo.save(menu_dir / "logo.tga", format="TGA")

	print(f"GameCube menu assets: baked {background_path.relative_to(output)} from {source_note}")
	return True


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
	"models/w_crowbar.mdl",
	# Small world NPC — gman (~76KB) OOMs libc malloc after crowbars on GC.
	"models/roach.mdl",
)

# Direct-load HUD sheets for lean New Game Redraw (ISO9660 sprites/ lookups
# false-miss like models/). Keep the set tiny — bootstrap MEM is tight.
GC_HUD_SPRITES = (
	"sprites/320_pain.spr",
	"sprites/320_train.spr",
	"sprites/crosshairs.spr",
	# Unique name so retail ISO9660 sprites/320hud2.spr cannot shadow a bad read.
	("sprites/320hud2.spr", "sprites/gc_320hud2.spr"),
)

# Lean skybox BMPs for New Game RGB565 fills. Use gc_desert* names so retail
# ISO9660 gfx/env/desert*.bmp cannot shadow a failed decode over the ZIP copy.
GC_SKY_SIDES = (
	("gfx/env/desertup.bmp", "gfx/env/gc_desertup.bmp"),
	("gfx/env/desertft.bmp", "gfx/env/gc_desertft.bmp"),
	("gfx/env/desertbk.bmp", "gfx/env/gc_desertbk.bmp"),
	("gfx/env/desertrt.bmp", "gfx/env/gc_desertrt.bmp"),
)


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
	"""Add lean 64×64 desert sky BMPs into bootstrap under gc_desert* names."""
	staged = 0
	for src_rel, arc_rel in GC_SKY_SIDES:
		src = data / src_rel
		payload = _downsample_bmp_to_bytes(src, 64)
		if not payload:
			continue
		arcname = arc_rel.lower()
		if arcname in archive.NameToInfo:
			continue
		archive.writestr(arcname, payload, compress_type=zipfile.ZIP_STORED)
		staged += 1
	return staged


def inject_gc_studio_into_bootstrap(archive: "zipfile.ZipFile", data: Path) -> int:
	"""Add allowlisted MDLs into an existing bootstrap ZIP under gc_studio/."""
	staged = 0
	for relative in GC_STUDIO_MODELS:
		src = data / relative
		if not src.is_file():
			continue
		arcname = f"gc_studio/{Path(relative).name.lower()}"
		archive.write(src, arcname, compress_type=zipfile.ZIP_STORED)
		staged += 1
	return staged


def inject_gc_hud_into_bootstrap(archive: "zipfile.ZipFile", data: Path) -> int:
	"""Add allowlisted HUD sprites into bootstrap under their disc paths."""
	staged = 0
	for entry in GC_HUD_SPRITES:
		if isinstance( entry, tuple ):
			src_rel, arc_rel = entry
		else:
			src_rel = arc_rel = entry
		src = data / src_rel
		if not src.is_file():
			continue
		arcname = arc_rel.lower()
		if arcname in archive.NameToInfo:
			continue
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


def write_smoke_overrides(
	output: Path,
	smoke_map: str,
	*,
	world_render: bool = False,
	phasetest: str | None = None,
	changelevel: str | None = None,
	landmark: str | None = None,
	leanpvs: bool = False,
) -> None:
	(output / "valve.rc").write_text("stuffcmds\n", encoding="ascii")
	(output / "config.cfg").write_text("\n", encoding="ascii")
	(output / "autoexec.cfg").write_text("\n", encoding="ascii")
	lines = [f"map {Path(smoke_map).stem}"]
	if changelevel:
		# G68: enable New Game PVS/present/changelevel path on the smoke map.
		lines.append("newgame")
	if world_render:
		lines.append("gcworldrender")
	if phasetest:
		lines.append(f"phasetest {phasetest}")
	if leanpvs:
		lines.append("leanpvs")
	if changelevel and landmark:
		lines.append(f"changelevel {Path(changelevel).stem} {landmark}")
	elif changelevel:
		lines.append(f"changelevel {Path(changelevel).stem}")
	elif landmark:
		lines.append(f"landmark {landmark}")
	(output / "gamecube.cfg").write_text("\n".join(lines) + "\n", encoding="ascii")
	media = output / "media"
	media.mkdir(exist_ok=True)
	(media / "StartupVids.txt").write_text("", encoding="ascii")


def write_probe_newgame_override(
	output: Path,
	newsaveload: bool = False,
	phasetest: str | None = None,
	changelevel: str | None = None,
	landmark: str | None = None,
	smoke_map: str | None = None,
	leanpvs: bool = False,
	fullphysics: bool = False,
) -> None:
	lines = []
	# G68/G100: bake map so -gcmap+newgame early changelevel path can plant inventory.
	if smoke_map:
		lines.append(f"map {Path(smoke_map).stem}")
	lines.append("newgame")
	if newsaveload:
		lines.append("newsaveload")
	if phasetest:
		lines.append(f"phasetest {phasetest}")
	if leanpvs:
		lines.append("leanpvs")
	if fullphysics:
		lines.append("fullphysics")
	if changelevel and landmark:
		lines.append(f"changelevel {Path(changelevel).stem} {landmark}")
	elif changelevel:
		lines.append(f"changelevel {Path(changelevel).stem}")
	elif landmark:
		lines.append(f"landmark {landmark}")
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
	logo_gcvid = build_logo_gcvid_companion(source, output)
	if logo_gcvid is not None:
		overlays.append(("media/logo.gcvid", logo_gcvid))
	startup_vids = media / "StartupVids.txt"

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
	still_frame = frames[still_frame_index]
	offsets: list[int] = []
	packets = bytearray()
	offsets.extend(0 for _ in range(actual_frames))
	packets.append(GCVID_KEYFRAME)
	if rgb565:
		for pixel in range(0, len(still_frame), 4):
			b = still_frame[pixel + 0]
			g = still_frame[pixel + 1]
			r = still_frame[pixel + 2]
			rgb565_pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
			packets.extend(struct.pack("<H", rgb565_pixel))
	else:
		packets.extend(still_frame)

	header = bytearray()
	header.extend(GCVID_MAGIC)
	header.extend(struct.pack("<I", width))
	header.extend(struct.pack("<I", height))
	header.extend(struct.pack("<I", fps_num))
	header.extend(struct.pack("<I", fps_den))
	header.extend(struct.pack("<I", actual_frames))
	flags = tile_size | GCVID_FLAG_STATIC_HOLD
	if not rgb565:
		flags |= GCVID_FLAG_BGRA32
	header.extend(struct.pack("<I", flags))
	for offset in offsets:
		header.extend(struct.pack("<I", offset))

	output_movie.parent.mkdir(parents=True, exist_ok=True)
	output_movie.write_bytes(bytes(header) + bytes(packets))
	print(
		f"Built static-hold GCVID {output_movie.name} from frame {still_frame_index} "
		f"({width}x{height}, duration {actual_frames} frames, "
		f"{'rgb565' if rgb565 else 'bgra32'})"
	)


def build_intro_gcvid_companions(output: Path) -> None:
	for relative in SMOKE_INTRO_MEDIA:
		source_movie = output / relative
		if not source_movie.is_file():
			continue
		gcvid = source_movie.with_suffix(".gcvid")
		build_gcvid_companion(source_movie, gcvid, rgb565=True)
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
	world_render: bool = False,
	phasetest: str | None = None,
	changelevel: str | None = None,
	landmark: str | None = None,
	leanpvs: bool = False,
) -> Path:
	map_name = smoke_map if smoke_map.endswith(".bsp") else f"{smoke_map}.bsp"
	map_relative = f"maps/{map_name}"
	map_source = source / map_relative
	if not map_source.is_file():
		raise FileNotFoundError(f"smoke map does not exist: {map_source}")

	output.mkdir(parents=True, exist_ok=True)
	for relative in SMOKE_CONFIG_FILES:
		copy_if_present(source, output, relative)
	for relative in MENU_RESOURCE_ASSETS:
		copy_if_present(source, output, relative)
	for relative in MENU_RESOURCE_DIRS:
		copy_tree_if_present(source, output, relative)
	write_smoke_overrides(
		output,
		smoke_map,
		world_render=world_render,
		phasetest=phasetest,
		changelevel=changelevel,
		landmark=landmark,
		leanpvs=leanpvs,
	)
	for relative in smoke_hud_resources(source):
		copy_if_present(source, output, relative)
	for relative in SMOKE_PRECACHE_MODELS:
		copy_if_present(source, output, relative)
	for relative in SMOKE_SOUND_DIRS:
		copy_tree_if_present(source, output, relative)
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
) -> None:
	output_path.parent.mkdir(parents=True, exist_ok=True)
	build_iso9660(data, extras, output_path, bootstrap_recursive, overlays)
	iso9660_size = output_path.stat().st_size
	dol_size = dol.stat().st_size
	dol_offset = align(iso9660_size, 0x800)
	# The boot process requires an FST, while game data is deliberately exposed
	# through ISO9660 so libc and filesystem_stdio can use normal POSIX calls.
	fst = struct.pack(">III", 0x01000000, 0, 1) + b"\0"
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
	header[APPLOADER_HEADER_OFFSET:APPLOADER_HEADER_OFFSET + 11] = b"2026/06/20\0"
	struct.pack_into(">I", header, APPLOADER_HEADER_OFFSET + 0x10, APPLOADER_ADDRESS)
	struct.pack_into(">I", header, APPLOADER_HEADER_OFFSET + 0x14, len(apploader))
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

def main() -> None:
	script_dir = Path(__file__).resolve().parent
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--dol", type=Path, default=Path("OUT/bin/boot.dol"))
	parser.add_argument("--data", type=Path, default=Path("Half-Life/valve"))
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
		"--probe-newsaveload",
		action="store_true",
		help="with --probe-newgame, also stage newsaveload for G94 RAM save/load probes",
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
			
	if args.smoke_map and args.intro_avi:
		parser.error("--smoke-map and --intro-avi are mutually exclusive")
	if args.smoke_map and args.probe_newgame:
		parser.error("--smoke-map and --probe-newgame are mutually exclusive")
	if args.probe_newsaveload and not args.probe_newgame:
		parser.error("--probe-newsaveload requires --probe-newgame")
	if args.world_render and not args.smoke_map:
		parser.error("--world-render requires --smoke-map")
	if args.probe_phasetest:
		phase = args.probe_phasetest.strip().lower()
		valid = {"early", "engine", "renderer", "sw_fb", "menu", "client", "intro", "map"}
		if phase not in valid:
			parser.error(f"--probe-phasetest must be one of: {', '.join(sorted(valid))}")
		args.probe_phasetest = phase

	# Full retail builds validate source, then stage a normalized copy for the ISO.
	if not args.smoke_map and not args.intro_avi:
		validation_errors = validate_assets(args.data)
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
				Path(temp) / "valve",
				args.smoke_map,
				world_render=args.world_render,
				phasetest=args.probe_phasetest,
				changelevel=args.probe_changelevel,
				landmark=args.probe_landmark,
				leanpvs=args.probe_leanpvs,
			)
			validation_errors = validate_smoke_assets(smoke_data, args.smoke_map)
			if validation_errors:
				print("Smoke asset validation failed:", file=sys.stderr)
				for error in validation_errors:
					print(f"  - {error}", file=sys.stderr)
				sys.exit(1)
			build_disc(
				args.dol,
				smoke_data,
				extras,
				args.output,
				args.apploader_source,
				args.apploader_linker,
				bootstrap_recursive=True,
			)
	elif args.intro_avi:
		with tempfile.TemporaryDirectory(prefix="xash3d-gc-intro-avi-data-") as temp:
			intro_data = stage_intro_avi_data(args.data, Path(temp) / "valve")
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
			)
	else:
		print("Staging retail Half-Life assets for GameCube (source files are not modified).")
		with tempfile.TemporaryDirectory(prefix="xash3d-gc-retail-data-") as temp:
			staged_root = Path(temp) / "valve"
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
					phasetest=args.probe_phasetest,
					changelevel=args.probe_changelevel,
					landmark=args.probe_landmark,
					# G68/G100: start map for -gcmap early changelevel + landmark plant.
					smoke_map="c0a0" if args.probe_changelevel else None,
					leanpvs=args.probe_leanpvs,
					fullphysics=args.probe_fullphysics,
				)
			elif args.probe_phasetest:
				write_probe_phasetest_override(staged_data, args.probe_phasetest)

			overlay_root = Path(temp) / "overlay" / "valve"
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
			)


if __name__ == "__main__":
	main()
