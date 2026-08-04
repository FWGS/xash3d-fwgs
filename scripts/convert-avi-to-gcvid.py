#!/usr/bin/env python3
"""Convert an AVI/movie source into a native GameCube GCVID companion."""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path


def load_encoder():
	module_path = Path(__file__).with_name("build-gamecube-disc.py")
	spec = importlib.util.spec_from_file_location("gamecube_disc_encoder", module_path)
	if spec is None or spec.loader is None:
		raise RuntimeError(f"cannot load encoder: {module_path}")
	module = importlib.util.module_from_spec(spec)
	sys.modules[spec.name] = module
	spec.loader.exec_module(module)
	return module.build_gcvid_companion


def main() -> int:
	parser = argparse.ArgumentParser(
		description="Convert a movie to a GameCube-native RGB565 delta GCVID stream"
	)
	parser.add_argument("source", type=Path, help="input AVI or ffmpeg-readable movie")
	parser.add_argument("output", type=Path, help="output .gcvid path")
	parser.add_argument("--width", type=int, default=320)
	parser.add_argument("--height", type=int, default=240)
	parser.add_argument("--fps", type=int, default=15)
	parser.add_argument("--still-frame", type=int, default=80)
	args = parser.parse_args()

	if not args.source.is_file():
		parser.error(f"input movie does not exist: {args.source}")
	if args.width <= 0 or args.height <= 0 or args.fps <= 0:
		parser.error("width, height, and fps must be positive")

	load_encoder()(
		args.source.resolve(),
		args.output.resolve(),
		width=args.width,
		height=args.height,
		fps_num=args.fps,
		fps_den=1,
		still_frame_index=args.still_frame,
		rgb565=True,
	)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
