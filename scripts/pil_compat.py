"""Pillow resize fallback for the GameCube disc build.

Ubuntu python3-pil 12.1.1 on Python 3.14 (this build host, Nov 2026) raises
SystemError from Image.resize with the NEAREST filter in every mode, and P
and 1-bit images support no other filter.  The disc build resizes masks,
palettised art and pixel-exact textures with NEAREST (world texture packs,
menu and loading art), so it failed for any map whose pack was not cached.

install() wraps Image.Image.resize: the native call runs first and its
result is used whenever it works; only when a NEAREST resize raises that
SystemError is it done here in numpy, with Pillow's own sampling rule
(source index = floor((dst + 0.5) * src / dst), or within box when given),
keeping the mode, palette and transparency.
"""
from __future__ import annotations

_installed = False


def _nearest(image, size, box):
	import numpy as np
	from PIL import Image

	w, h = image.size
	dw, dh = int(size[0]), int(size[1])
	x0, y0, x1, y1 = box if box is not None else (0, 0, w, h)
	xs = np.floor(x0 + (np.arange(dw) + 0.5) * (x1 - x0) / dw).astype(np.int64).clip(0, w - 1)
	ys = np.floor(y0 + (np.arange(dh) + 0.5) * (y1 - y0) / dh).astype(np.int64).clip(0, h - 1)
	mode = image.mode
	if mode == "P":
		src = np.frombuffer(image.tobytes(), np.uint8).reshape(h, w)
		res = Image.frombytes("P", (dw, dh), np.ascontiguousarray(src[ys][:, xs]).tobytes())
		res.putpalette(image.getpalette())
		res.info.update(image.info)
		return res
	src = np.asarray(image)
	out = np.ascontiguousarray(src[ys][:, xs])
	if mode == "1":
		return Image.fromarray(out.astype(bool))
	return Image.fromarray(out, mode)


def install() -> None:
	global _installed
	if _installed:
		return
	try:
		from PIL import Image
	except ImportError:
		return
	native = Image.Image.resize

	def resize(self, size, resample=None, box=None, reducing_gap=None):
		try:
			return native(self, size, resample, box, reducing_gap)
		except SystemError:
			nearest = resample in (None, Image.Resampling.NEAREST) if self.mode in ("P", "1") \
				else resample == Image.Resampling.NEAREST
			if not nearest:
				raise
			return _nearest(self, size, box)

	Image.Image.resize = resize
	_installed = True
