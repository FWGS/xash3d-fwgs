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
    ".avi", ".wmv", ".mpg", ".mpeg",  # Video: unsupported or too heavy
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
GCVID_WIDTH = 320
GCVID_HEIGHT = 240
GCVID_FPS = 15
GCVID_HEADER = struct.Struct("<4sIIIII")


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
				if (
					child.is_file()
					and child.suffix.lower() not in BOOTSTRAP_EXCLUDED_EXTENSIONS
					and child.stat().st_size <= 2 * 1024 * 1024
				):
					compress_type = (
						zipfile.ZIP_STORED
						if child.suffix.lower() in (".bsp", ".mdl")
						else zipfile.ZIP_DEFLATED
					)
					archive.write(
						child,
						child.relative_to(data).as_posix(),
						compress_type=compress_type,
					)

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


def convert_intro_media(source: Path, output: Path) -> tuple[tuple[str, Path], ...]:
	ffmpeg = shutil.which("ffmpeg")
	converted: list[tuple[str, Path]] = []

	for relative in SMOKE_INTRO_MEDIA:
		avi = source / relative
		if not avi.is_file():
			continue
		if ffmpeg is None:
			raise FileNotFoundError("ffmpeg is required to convert Half-Life startup AVI files")

		gcvid_relative = Path(relative).with_suffix(".gcvid").as_posix()
		gcvid = output / gcvid_relative
		if gcvid.is_file() and gcvid.stat().st_mtime >= avi.stat().st_mtime:
			converted.append((gcvid_relative, gcvid))
			continue

		gcvid.parent.mkdir(parents=True, exist_ok=True)
		convert_avi_to_gcvid(ffmpeg, avi, gcvid)
		converted.append((gcvid_relative, gcvid))

	return tuple(converted)


def convert_avi_to_gcvid(ffmpeg: str, source: Path, output: Path) -> None:
	frame_size = GCVID_WIDTH * GCVID_HEIGHT * 4
	temp_output = output.with_suffix(output.suffix + ".tmp")
	command = [
		ffmpeg,
		"-v", "error",
		"-i", str(source),
		"-an",
		"-vf",
		f"scale={GCVID_WIDTH}:{GCVID_HEIGHT}:force_original_aspect_ratio=decrease,"
		f"pad={GCVID_WIDTH}:{GCVID_HEIGHT}:(ow-iw)/2:(oh-ih)/2,fps={GCVID_FPS}",
		"-f", "rawvideo",
		"-pix_fmt", "bgra",
		"-",
	]

	frame_count = 0
	process = subprocess.Popen(command, stdout=subprocess.PIPE)
	assert process.stdout is not None
	try:
		with temp_output.open("wb") as out:
			out.write(GCVID_HEADER.pack(b"GCV1", GCVID_WIDTH, GCVID_HEIGHT, GCVID_FPS, 1, 0))
			while True:
				frame = process.stdout.read(frame_size)
				if not frame:
					break
				if len(frame) != frame_size:
					raise ValueError(f"partial video frame while converting {source}")
				out.write(frame)
				frame_count += 1
	finally:
		process.stdout.close()

	if process.wait() != 0:
		temp_output.unlink(missing_ok=True)
		raise subprocess.CalledProcessError(process.returncode, command)
	if frame_count == 0:
		temp_output.unlink(missing_ok=True)
		raise ValueError(f"ffmpeg produced no frames for {source}")

	with temp_output.open("r+b") as out:
		out.write(GCVID_HEADER.pack(b"GCV1", GCVID_WIDTH, GCVID_HEIGHT, GCVID_FPS, 1, frame_count))
	temp_output.replace(output)

SMOKE_HUD_RES = 320

SMOKE_HUD_SPRITES = (
	"sprites/animglow01.spr",
	"sprites/dot.spr",
	"sprites/hud.txt",
	"sprites/muzzleflash1.spr",
	"sprites/muzzleflash2.spr",
	"sprites/muzzleflash3.spr",
	"sprites/richo1.spr",
	"sprites/shellchrome.spr",
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


def write_smoke_overrides(output: Path, smoke_map: str) -> None:
	(output / "valve.rc").write_text("stuffcmds\n", encoding="ascii")
	(output / "config.cfg").write_text("\n", encoding="ascii")
	(output / "autoexec.cfg").write_text("\n", encoding="ascii")
	(output / "gamecube.cfg").write_text(f"map {Path(smoke_map).stem}\n", encoding="ascii")
	media = output / "media"
	media.mkdir(exist_ok=True)
	(media / "StartupVids.txt").write_text("", encoding="ascii")


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


def create_startup_vids_overlay(source: Path, output: Path) -> tuple[tuple[str, Path], ...]:
	movies = [
		relative
		for relative in SMOKE_INTRO_MEDIA
		if (source / relative).is_file()
	]
	if not movies:
		return ()

	path = output / "media" / "StartupVids.txt"
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text("".join(f"{movie}\n" for movie in movies), encoding="ascii")
	return (("media/StartupVids.txt", path),)


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


def stage_smoke_data(source: Path, output: Path, smoke_map: str) -> Path:
	map_name = smoke_map if smoke_map.endswith(".bsp") else f"{smoke_map}.bsp"
	map_relative = f"maps/{map_name}"
	map_source = source / map_relative
	if not map_source.is_file():
		raise FileNotFoundError(f"smoke map does not exist: {map_source}")

	output.mkdir(parents=True, exist_ok=True)
	for relative in SMOKE_CONFIG_FILES:
		copy_if_present(source, output, relative)
	write_smoke_overrides(output, smoke_map)
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

	(output / "custom").mkdir(exist_ok=True)
	(output / "maps").mkdir(exist_ok=True)
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
	Validate the Half-Life valve directory for GameCube staging.
	Returns a list of error messages. Empty list means validation passed.
	"""
	errors = []
	
	if not data_path.is_dir():
		return [f"Data directory not found: {data_path}"]

	# 1. Check for critical assets
	for asset in CRITICAL_ASSETS:
		asset_path = data_path / asset
		if not asset_path.exists():
			errors.append(f"MISSING: Critical asset '{asset}' is not present.")
		elif not asset_path.is_file():
			errors.append(f"ERROR: Critical path '{asset}' exists but is not a file.")

	# 2. Scan for case mismatches, unsupported extensions, and oversized files
	# We scan the entire valve directory
	for root, dirs, files in os.walk(data_path):
		rel_root = os.path.relpath(root, data_path)
		# Check if this directory is one of the strict case directories
		in_strict_dir = any(rel_root.startswith(d) or rel_root == d for d in STRICT_CASE_DIRS)
		
		for filename in files:
			filepath = data_path / rel_root / filename
			rel_path = os.path.join(rel_root, filename).replace("\\", "/")
			
			# Check case mismatch
			if in_strict_dir and filename != filename.lower():
				# Allow some known exceptions if necessary, but generally HL is lowercase
				errors.append(f"CASE_MISMATCH: '{rel_path}' contains uppercase characters. "
				               "GameCube/Engine expects lowercase.")
			
			# Check unsupported extensions
			_, ext = os.path.splitext(filename)
			if ext.lower() in UNSUPPORTED_EXTENSIONS:
				errors.append(f"UNSUPPORTED: '{rel_path}' has extension '{ext}' which is not supported on GameCube.")
			
			# Check size
			try:
				size = filepath.stat().st_size
				if size > MAX_ASSET_SIZE:
					errors.append(f"OVERSIZED: '{rel_path}' is {size} bytes. "
					               f"Limit is {MAX_ASSET_SIZE} bytes to prevent OOM.")
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
		"--apploader-source", type=Path, default=script_dir / "gamecube-apploader.c"
	)
	parser.add_argument(
		"--apploader-linker", type=Path, default=script_dir / "gamecube-apploader.ld"
	)
	args = parser.parse_args()

	for path in (args.dol, args.data):
		if not path.exists():
			parser.error(f"required path does not exist: {path}")
			
	# Run asset validation before building (skip for smoke-map builds which
	# stage a minimal subset independently)
	if not args.smoke_map:
		validation_errors = validate_assets(args.data)
		if validation_errors:
			print("Asset validation failed:", file=sys.stderr)
			for error in validation_errors:
				print(f"  - {error}", file=sys.stderr)
			sys.exit(1)
		
	extras = args.extras if args.extras.exists() else None
	if args.smoke_map:
		with tempfile.TemporaryDirectory(prefix="xash3d-gc-smoke-data-") as temp:
			smoke_data = stage_smoke_data(args.data, Path(temp) / "valve", args.smoke_map)
			build_disc(
				args.dol,
				smoke_data,
				extras,
				args.output,
				args.apploader_source,
				args.apploader_linker,
				bootstrap_recursive=True,
			)
	else:
		with tempfile.TemporaryDirectory(prefix="xash3d-gc-intro-media-") as temp:
			overlay_root = Path(temp) / "valve"
			overlays = (
				convert_intro_media(args.data, overlay_root)
				+ create_startup_vids_overlay(args.data, overlay_root)
			)
			build_disc(
				args.dol,
				args.data,
				extras,
				args.output,
				args.apploader_source,
				args.apploader_linker,
				overlays=overlays,
			)


if __name__ == "__main__":
	main()
