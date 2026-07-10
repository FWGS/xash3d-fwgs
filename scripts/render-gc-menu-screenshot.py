#!/usr/bin/env python3
"""Render retail vs GameCube main-menu comparison images from valve assets."""

from __future__ import annotations

import argparse
import importlib.util
import sys
import tempfile
from pathlib import Path


def import_disc_builder():
	name = "build_gamecube_disc"
	path = Path(__file__).resolve().parent / "build-gamecube-disc.py"
	spec = importlib.util.spec_from_file_location(name, path)
	module = importlib.util.module_from_spec(spec)
	sys.modules[name] = module
	assert spec.loader is not None
	spec.loader.exec_module(module)
	return module


def load_pil():
	try:
		from PIL import Image, ImageDraw, ImageFont
	except ImportError as exc:
		raise SystemExit("Pillow is required: pip install pillow") from exc
	return Image, ImageDraw, ImageFont


def retail_reference(source: Path) -> "Image.Image":
	Image, _, _ = load_pil()
	disc = import_disc_builder()

	for name in ("HD_BackgroundLayout.txt", "BackgroundLayout.txt"):
		layout = source / "resource" / name
		if not layout.is_file():
			continue
		bg_w, bg_h, tiles = disc._gc_menu_parse_layout(source, layout)
		if not tiles:
			continue
		canvas = Image.new("RGBA", (bg_w, bg_h), (24, 28, 40, 255))
		for tile_path, x, y in tiles:
			canvas.paste(Image.open(tile_path).convert("RGBA"), (x, y))
		return disc._gc_menu_boost_visibility(canvas)
	raise FileNotFoundError("no usable BackgroundLayout in valve tree")


def gc_menu_preview(source: Path, output_root: Path) -> "Image.Image":
	disc = import_disc_builder()

	Image, ImageDraw, _ = load_pil()
	disc.stage_gc_menu_assets(source, output_root)
	bg_path = output_root / "resource" / "gc_menu" / "background.tga"
	logo_path = output_root / "resource" / "gc_menu" / "logo.tga"
	if not bg_path.is_file():
		raise FileNotFoundError(f"missing baked menu background: {bg_path}")

	canvas = Image.open(bg_path).convert("RGBA").resize((640, 480), Image.Resampling.LANCZOS)
	draw = ImageDraw.Draw(canvas)
	if logo_path.is_file():
		logo = Image.open(logo_path).convert("RGBA")
		logo = logo.resize((int(640 * 0.70), int(640 * 0.70 / 16)), Image.Resampling.LANCZOS)
		canvas.paste(logo, (int((640 - logo.width) / 2), int(480 * 0.12)), logo)

	menu_x, menu_y = 70, 252
	for index, label in enumerate(("New Game", "Load Game", "Options")):
		color = (255, 214, 48, 255) if index == 0 else (224, 170, 16, 255)
		draw.text((menu_x, menu_y + index * 34), label, fill=color)
	draw.text((menu_x, menu_y + 3 * 34 + 34), "Start a new single player game.", fill=(112, 112, 112, 255))
	return canvas


def side_by_side(left, right) -> "Image.Image":
	Image, _, _ = load_pil()
	out = Image.new("RGBA", (left.width + right.width, max(left.height, right.height)), (0, 0, 0, 255))
	out.paste(left.convert("RGBA"), (0, 0))
	out.paste(right.convert("RGBA"), (left.width, 0))
	return out


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--data", type=Path, default=Path("Half-Life/valve"))
	parser.add_argument("--out-dir", type=Path, default=Path(".ai/screenshots"))
	args = parser.parse_args()

	source = args.data.resolve()
	out_dir = args.out_dir.resolve()
	out_dir.mkdir(parents=True, exist_ok=True)

	retail = retail_reference(source).resize((640, 480), load_pil()[0].Resampling.LANCZOS)
	retail_path = out_dir / "retail-main-menu-reference.png"
	retail.save(retail_path)

	with tempfile.TemporaryDirectory(prefix="xash3d-gc-menu-render-") as temp:
		gc = gc_menu_preview(source, Path(temp))
	gc_path = out_dir / "gc-main-menu.png"
	gc.save(gc_path)

	compare_path = out_dir / "gc-main-menu-vs-retail.png"
	side_by_side(retail, gc).save(compare_path)

	print(f"retail reference: {retail_path}")
	print(f"gc menu preview: {gc_path}")
	print(f"comparison: {compare_path}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
