#!/usr/bin/env python3
"""Bake offline CapFaces packs: media/cf_<map>.gcp

Portable geometry + LM atlas for GameCube CapFaces Flipper path.
Runtime hydrates CapFaces BSS and compiles a GX display list once.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

# Allow `from bake_capfaces_pack import ...` when imported by build-gamecube-disc.py
sys.path.insert(0, str(Path(__file__).resolve().parent))

GC_CF_MAGIC = b"GCcf"
GC_CF_VERSION = 1
GC_CF_FLAG_LM_ATLAS = 1
GC_MAX_CAP_FACES = 384  # 512 tipped GC MEM1; keep spatial coverage within tip
GC_CAP_MAX_VERTS = 8
GC_CAP_LM_DIM = 4
GC_LM_ATLAS_W = 128
GC_LM_ATLAS_H = 128
GC_LM_ATLAS_COLS = GC_LM_ATLAS_W // GC_CAP_LM_DIM
GC_LM_ATLAS_ROWS = GC_LM_ATLAS_H // GC_CAP_LM_DIM

# bake_src
BAKE_NONE = 0
BAKE_EDGE = 1
BAKE_PLANE = 2
BAKE_TEX = 3

FACE_REC = struct.Struct(">" + "BBHI" + "hhh" * GC_CAP_MAX_VERTS + "e" * (GC_CAP_MAX_VERTS * 4) + "BBH")
# BBHI = 8; pts 48; st+lm 64 halfs = 64; BBH = 4 → 124. Pad to 128 with 4 bytes.
FACE_REC_PAD = 4
assert FACE_REC.size + FACE_REC_PAD == 128, FACE_REC.size

HEADER = struct.Struct(">4sHHHHIHHHHII")  # see plan; we'll pack manually for clarity


def fnv1a32(s: str) -> int:
	h = 2166136261
	for c in s.encode("latin-1"):
		h ^= c
		h = (h * 16777619) & 0xFFFFFFFF
	return h


def f32_to_f16(x: float) -> int:
	"""IEEE binary16 via struct half ('e')."""
	return struct.unpack(">H", struct.pack(">e", float(x)))[0]


def lump(bsp: bytes, i: int) -> tuple[int, int]:
	return struct.unpack_from("<ii", bsp, 4 + i * 8)


def load_bsp(path: Path) -> dict:
	data = path.read_bytes()
	ver = struct.unpack_from("<i", data, 0)[0]
	if ver not in (29, 30):
		raise ValueError(f"{path}: unsupported BSP version {ver}")

	def get(i):
		off, size = lump(data, i)
		return data[off : off + size], off, size

	ents, _, _ = get(0)
	planes_raw, _, _ = get(1)
	tex_raw, _, _ = get(2)
	verts_raw, _, _ = get(3)
	nodes_raw, _, _ = get(5)
	texinfo_raw, _, _ = get(6)
	faces_raw, _, _ = get(7)
	lighting, _, _ = get(8)
	edges_raw, _, _ = get(12)
	surfedges_raw, _, _ = get(13)

	verts = [struct.unpack_from("<fff", verts_raw, i) for i in range(0, len(verts_raw), 12)]
	# edgedef: short v[2]
	edges = [struct.unpack_from("<hh", edges_raw, i) for i in range(0, len(edges_raw), 4)]
	surfedges = list(struct.unpack_from(f"<{len(surfedges_raw)//4}i", surfedges_raw))
	# dface_t HL: planenum u16, side u16, firstedge i32, numedges i16, texinfo i16, styles[4], lightofs i32
	faces = []
	for i in range(0, len(faces_raw), 20):
		planenum, side, firstedge, numedges, texinfo, s0, s1, s2, s3, lightofs = struct.unpack_from(
			"<HHiHHBBBBi", faces_raw, i
		)
		faces.append(
			{
				"planenum": planenum,
				"side": side,
				"firstedge": firstedge,
				"numedges": numedges,
				"texinfo": texinfo,
				"styles": (s0, s1, s2, s3),
				"lightofs": lightofs,
			}
		)
	# dplane_t: normal[3] f32, dist f32, type i32
	planes = []
	for i in range(0, len(planes_raw), 20):
		nx, ny, nz, dist, typ = struct.unpack_from("<ffffi", planes_raw, i)
		planes.append(((nx, ny, nz), dist, typ))
	# texinfo: vecs[2][4] f32, miptex i32, flags i32
	texinfos = []
	for i in range(0, len(texinfo_raw), 40):
		vals = struct.unpack_from("<8fii", texinfo_raw, i)
		texinfos.append({"vecs": (vals[0:4], vals[4:8]), "miptex": vals[8], "flags": vals[9]})

	# miptex directory
	tex_names: list[str] = []
	if len(tex_raw) >= 4:
		count = struct.unpack_from("<i", tex_raw, 0)[0]
		for j in range(count):
			off = struct.unpack_from("<i", tex_raw, 4 + j * 4)[0]
			if off < 0 or off + 16 > len(tex_raw):
				tex_names.append("")
				continue
			name = tex_raw[off : off + 16].split(b"\0")[0].decode("latin-1", "replace").lower()
			tex_names.append(name)

	return {
		"data": data,
		"verts": verts,
		"edges": edges,
		"surfedges": surfedges,
		"faces": faces,
		"planes": planes,
		"texinfos": texinfos,
		"tex_names": tex_names,
		"lighting": lighting,
	}


def face_verts(bsp: dict, face: dict) -> list[tuple[float, float, float]]:
	pts = []
	fe = face["firstedge"]
	ne = face["numedges"]
	for i in range(ne):
		se = bsp["surfedges"][fe + i]
		if se >= 0:
			e = bsp["edges"][se]
			v = bsp["verts"][e[0]]
		else:
			e = bsp["edges"][-se]
			v = bsp["verts"][e[1]]
		pts.append(v)
	return pts


def decimate_ring(pts: list, maxv: int = GC_CAP_MAX_VERTS) -> list:
	if len(pts) <= maxv:
		return pts
	# keep evenly spaced including first
	out = []
	n = len(pts)
	for i in range(maxv):
		out.append(pts[(i * n) // maxv])
	return out


def tex_st(texinfo: dict, p: tuple[float, float, float], tw: float, th: float) -> tuple[float, float]:
	v0, v1 = texinfo["vecs"]
	s = p[0] * v0[0] + p[1] * v0[1] + p[2] * v0[2] + v0[3]
	t = p[0] * v1[0] + p[1] * v1[1] + p[2] * v1[2] + v1[3]
	if tw < 1:
		tw = 64
	if th < 1:
		th = 64
	return s / tw, t / th


def extents_area(pts: list, texinfo: dict) -> int:
	if len(pts) < 3:
		return 0
	v0, v1 = texinfo["vecs"]
	ss = [p[0] * v0[0] + p[1] * v0[1] + p[2] * v0[2] + v0[3] for p in pts]
	tt = [p[0] * v1[0] + p[1] * v1[1] + p[2] * v1[2] + v1[3] for p in pts]
	return max(1, int((max(ss) - min(ss)) * (max(tt) - min(tt))))


def facing_score(plane_n, plane_d, eye) -> float:
	# positive when eye is in front of plane
	return eye[0] * plane_n[0] + eye[1] * plane_n[1] + eye[2] * plane_n[2] - plane_d


def sample_lm_rgb565(lighting: bytes, lightofs: int, styles, w: int, h: int) -> bytes:
	"""Nearest sample style-0 into GC_CAP_LM_DIM² RGB565 tile."""
	tile = bytearray(GC_CAP_LM_DIM * GC_CAP_LM_DIM * 2)
	if lightofs < 0 or not lighting or w < 1 or h < 1:
		# flat mid grey
		c = 0x8410
		for i in range(GC_CAP_LM_DIM * GC_CAP_LM_DIM):
			struct.pack_into(">H", tile, i * 2, c)
		return bytes(tile)
	# HL lightmap: styles[0] block is w*h*3 bytes at lightofs
	base = lightofs
	if styles[0] == 255:
		c = 0xFFFF
		for i in range(GC_CAP_LM_DIM * GC_CAP_LM_DIM):
			struct.pack_into(">H", tile, i * 2, c)
		return bytes(tile)
	need = w * h * 3
	if base + need > len(lighting):
		c = 0x8410
		for i in range(GC_CAP_LM_DIM * GC_CAP_LM_DIM):
			struct.pack_into(">H", tile, i * 2, c)
		return bytes(tile)
	for ty in range(GC_CAP_LM_DIM):
		for tx in range(GC_CAP_LM_DIM):
			sx = min(w - 1, (tx * w) // GC_CAP_LM_DIM)
			sy = min(h - 1, (ty * h) // GC_CAP_LM_DIM)
			o = base + (sy * w + sx) * 3
			r, g, b = lighting[o], lighting[o + 1], lighting[o + 2]
			rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
			struct.pack_into(">H", tile, (ty * GC_CAP_LM_DIM + tx) * 2, rgb565)
	return bytes(tile)


def pick_eyes(spawns: dict | None, map_stem: str) -> list[dict]:
	eyes = []
	info = (spawns or {}).get(map_stem) or (spawns or {}).get(map_stem.lower())
	if not info:
		return eyes
	for key in ("axis", "allies"):
		e = info.get(key)
		if e and "origin" in e:
			eyes.append({"origin": e["origin"], "angles": e.get("angles", [0, 0, 0])})
	# spread extras
	extras = []
	for key in ("axis_all", "allies_all"):
		extras.extend(info.get(key) or [])
	# pick up to 4 more farthest from already chosen
	while len(eyes) < 6 and extras:
		best_i, best_d = 0, -1.0
		for i, e in enumerate(extras):
			o = e["origin"]
			d = min(
				(o[0] - x["origin"][0]) ** 2 + (o[1] - x["origin"][1]) ** 2 + (o[2] - x["origin"][2]) ** 2
				for x in eyes
			)
			if d > best_d:
				best_d, best_i = d, i
		e = extras.pop(best_i)
		eyes.append({"origin": e["origin"], "angles": e.get("angles", [0, 0, 0])})
	return eyes


def bake_map(
	bsp_path: Path,
	spawns: dict | None,
	out_path: Path,
) -> dict:
	bsp = load_bsp(bsp_path)
	stem = bsp_path.stem.lower()
	eyes = pick_eyes(spawns, stem)
	if not eyes:
		# map center fallback
		xs = [v[0] for v in bsp["verts"]]
		ys = [v[1] for v in bsp["verts"]]
		zs = [v[2] for v in bsp["verts"]]
		eyes = [
			{
				"origin": [(min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5, (min(zs) + max(zs)) * 0.5 + 64],
				"angles": [0.0, 0.0, 0.0],
			}
		]

	SURF_SKY = 4
	SURF_WARP = 8
	SURF_TRANS = 16
	# GoldSrc texinfo flags vary; skip by name
	cands = []
	for fi, face in enumerate(bsp["faces"]):
		if face["numedges"] < 3:
			continue
		ti = face["texinfo"]
		if ti < 0 or ti >= len(bsp["texinfos"]):
			continue
		texinfo = bsp["texinfos"][ti]
		mip = texinfo["miptex"]
		name = bsp["tex_names"][mip] if 0 <= mip < len(bsp["tex_names"]) else ""
		if not name or name.startswith("sky") or name.startswith("!") or name.startswith("*"):
			continue
		if texinfo["flags"] & (SURF_SKY | SURF_WARP | SURF_TRANS):
			continue
		pts = face_verts(bsp, face)
		if len(pts) < 3:
			continue
		area = extents_area(pts, texinfo)
		if area < 384:
			continue
		pl = bsp["planes"][face["planenum"] % len(bsp["planes"])]
		n, dist, _ = pl
		if face["side"]:
			n = (-n[0], -n[1], -n[2])
			dist = -dist
		score = 0.0
		for eye in eyes:
			fs = facing_score(n, dist, eye["origin"])
			if fs > 0:
				# distance attenuation
				cx = sum(p[0] for p in pts) / len(pts)
				cy = sum(p[1] for p in pts) / len(pts)
				cz = sum(p[2] for p in pts) / len(pts)
				dx = cx - eye["origin"][0]
				dy = cy - eye["origin"][1]
				dz = cz - eye["origin"][2]
				d2 = dx * dx + dy * dy + dz * dz
				score += area * fs / (1.0 + d2 * 0.00001)
		if score <= 0:
			continue
		cx = sum(p[0] for p in pts) / len(pts)
		cy = sum(p[1] for p in pts) / len(pts)
		cz = sum(p[2] for p in pts) / len(pts)
		cands.append((score, area, fi, name, texinfo, n, dist, pts, cx, cy, cz))

	# Prefer spatial coverage over pure near-eye score so CapFaces lean can
	# paint more of the map; PVS+frustum then drop off-screen faces at runtime.
	cands.sort(key=lambda x: -x[0])
	if len(cands) > GC_MAX_CAP_FACES:
		cell = 512.0
		buckets: dict[tuple[int, int, int], list] = {}
		for c in cands:
			key = (int(c[8] // cell), int(c[9] // cell), int(c[10] // cell))
			buckets.setdefault(key, []).append(c)
		per = max(1, GC_MAX_CAP_FACES // max(1, len(buckets)))
		picked = []
		# Round-robin: take top-N from each cell, then fill by global score.
		for _ in range(per):
			for key in list(buckets.keys()):
				if len(picked) >= GC_MAX_CAP_FACES:
					break
				if buckets[key]:
					picked.append(buckets[key].pop(0))
			if len(picked) >= GC_MAX_CAP_FACES:
				break
		if len(picked) < GC_MAX_CAP_FACES:
			have = {id(x) for x in picked}
			for c in cands:
				if len(picked) >= GC_MAX_CAP_FACES:
					break
				if id(c) not in have:
					picked.append(c)
		cands = picked
	else:
		cands = cands[:GC_MAX_CAP_FACES]
	# Drop centroid fields for the emit loop below.
	cands = [c[:8] for c in cands]

	atlas = bytearray(GC_LM_ATLAS_W * GC_LM_ATLAS_H * 2)
	# clear to dark
	for i in range(0, len(atlas), 2):
		atlas[i], atlas[i + 1] = 0x18, 0xC3  # ~ dark greenish 0x18C3

	faces_out = []
	order_keys = []
	for slot, (score, area, fi, name, texinfo, n, dist, pts) in enumerate(cands):
		face = bsp["faces"][fi]
		dpts = decimate_ring(pts, GC_CAP_MAX_VERTS)
		bake_src = BAKE_EDGE if len(pts) <= GC_CAP_MAX_VERTS else BAKE_TEX
		# lightmap size from extents
		v0, v1 = texinfo["vecs"]
		ss = [p[0] * v0[0] + p[1] * v0[1] + p[2] * v0[2] + v0[3] for p in pts]
		tt = [p[0] * v1[0] + p[1] * v1[1] + p[2] * v1[2] + v1[3] for p in pts]
		lm_w = max(1, int(math.ceil((max(ss) - min(ss)) / 16)) + 1)
		lm_h = max(1, int(math.ceil((max(tt) - min(tt)) / 16)) + 1)
		lm_w = min(lm_w, 17)
		lm_h = min(lm_h, 17)
		col = slot % GC_LM_ATLAS_COLS
		row = slot // GC_LM_ATLAS_COLS
		if row >= GC_LM_ATLAS_ROWS:
			break
		tile = sample_lm_rgb565(bsp["lighting"], face["lightofs"], face["styles"], lm_w, lm_h)
		# blit tile into atlas (row-major RGB565)
		for ty in range(GC_CAP_LM_DIM):
			for tx in range(GC_CAP_LM_DIM):
				src = (ty * GC_CAP_LM_DIM + tx) * 2
				dx = col * GC_CAP_LM_DIM + tx
				dy = row * GC_CAP_LM_DIM + ty
				dst = (dy * GC_LM_ATLAS_W + dx) * 2
				atlas[dst : dst + 2] = tile[src : src + 2]

		nv = len(dpts)
		pts_s16 = []
		st = []
		lm_uv = []
		for i in range(GC_CAP_MAX_VERTS):
			if i < nv:
				p = dpts[i]
				pts_s16.extend([int(max(-32767, min(32767, round(p[0])))), int(max(-32767, min(32767, round(p[1])))), int(max(-32767, min(32767, round(p[2]))))])
				s, t = tex_st(texinfo, p, 64, 64)  # wt_ normalizes; 64 placeholder OK for pack ST
				# better: use actual mip size unknown — ST in world luxels / 64 is fine for CMPR bind with R_GXFaceST style
				st.extend([s, t])
				# LM UV into atlas tile
				u = (col * GC_CAP_LM_DIM + 0.5 + (i / max(1, nv - 1)) * (GC_CAP_LM_DIM - 1)) / GC_LM_ATLAS_W
				v = (row * GC_CAP_LM_DIM + 0.5) / GC_LM_ATLAS_H
				# better LM: project onto lightmap
				s_lux = p[0] * v0[0] + p[1] * v0[1] + p[2] * v0[2] + v0[3]
				t_lux = p[0] * v1[0] + p[1] * v1[1] + p[2] * v1[2] + v1[3]
				ls = (s_lux - min(ss)) / 16.0
				lt = (t_lux - min(tt)) / 16.0
				ls = max(0.0, min(float(lm_w - 1), ls)) / max(1, lm_w - 1) if lm_w > 1 else 0.5
				lt = max(0.0, min(float(lm_h - 1), lt)) / max(1, lm_h - 1) if lm_h > 1 else 0.5
				u = (col * GC_CAP_LM_DIM + 0.5 + ls * (GC_CAP_LM_DIM - 1)) / GC_LM_ATLAS_W
				v = (row * GC_CAP_LM_DIM + 0.5 + lt * (GC_CAP_LM_DIM - 1)) / GC_LM_ATLAS_H
				lm_uv.extend([u, v])
			else:
				pts_s16.extend([0, 0, 0])
				st.extend([0.0, 0.0])
				lm_uv.extend([0.0, 0.0])

		tex_hash = fnv1a32(name)
		faces_out.append(
			{
				"nverts": nv,
				"bake_src": bake_src,
				"flags": 0,
				"area": area,
				"tex_hash": tex_hash,
				"tex_name": name,
				"pts": pts_s16,
				"st": st,
				"lm_uv": lm_uv,
				"lm_col": col,
				"lm_row": row,
			}
		)
		order_keys.append((tex_hash, -area, slot))

	order_keys.sort()
	order = [slot for _, _, slot in order_keys]
	# remap order indices after possible early break
	order = [i for i in order if i < len(faces_out)]

	# Build binary — 32-byte header:
	# 0 magic4, 4 ver u16, 6 flags u16, 8 eye u16, 10 face u16,
	# 12 lm_atlas_ofs u32, 16 lm_w u16, 18 lm_h u16, 20 order_count u16, 22 pad u16,
	# 24 faces_ofs u32, 28 order_ofs u32
	eye_blob = bytearray()
	for e in eyes:
		o, a = e["origin"], e["angles"]
		eye_blob += struct.pack(
			">ffffff",
			float(o[0]), float(o[1]), float(o[2]),
			float(a[0]), float(a[1]), float(a[2]),
		)

	hdr_size = 32
	eyes_ofs = hdr_size
	eyes_bytes = len(eye_blob)
	face_bytes = len(faces_out) * 128
	order_bytes = len(order) * 2
	faces_ofs = (eyes_ofs + eyes_bytes + 31) & ~31
	order_ofs = faces_ofs + face_bytes
	atlas_ofs = (order_ofs + order_bytes + 31) & ~31
	atlas_bytes = len(atlas)
	buf = bytearray(atlas_ofs + atlas_bytes)

	buf[0:4] = GC_CF_MAGIC
	struct.pack_into(">HHHH", buf, 4, GC_CF_VERSION, GC_CF_FLAG_LM_ATLAS, len(eyes), len(faces_out))
	struct.pack_into(">I", buf, 12, atlas_ofs)
	struct.pack_into(">HHHH", buf, 16, GC_LM_ATLAS_W, GC_LM_ATLAS_H, len(order), 0)
	struct.pack_into(">II", buf, 24, faces_ofs, order_ofs)

	buf[eyes_ofs : eyes_ofs + eyes_bytes] = eye_blob

	for i, f in enumerate(faces_out):
		off = faces_ofs + i * 128
		buf[off] = f["nverts"] & 0xFF
		buf[off + 1] = f["bake_src"] & 0xFF
		struct.pack_into(">H", buf, off + 2, f["flags"])
		struct.pack_into(">I", buf, off + 4, f["area"] & 0xFFFFFFFF)
		struct.pack_into(">I", buf, off + 8, f["tex_hash"])
		for v in range(GC_CAP_MAX_VERTS):
			struct.pack_into(
				">hhh",
				buf,
				off + 12 + v * 6,
				f["pts"][v * 3],
				f["pts"][v * 3 + 1],
				f["pts"][v * 3 + 2],
			)
		# st halfs at off+60, lm at off+92
		for v in range(GC_CAP_MAX_VERTS):
			struct.pack_into(">e", buf, off + 60 + v * 4, float(f["st"][v * 2]))
			struct.pack_into(">e", buf, off + 60 + v * 4 + 2, float(f["st"][v * 2 + 1]))
			struct.pack_into(">e", buf, off + 92 + v * 4, float(f["lm_uv"][v * 2]))
			struct.pack_into(">e", buf, off + 92 + v * 4 + 2, float(f["lm_uv"][v * 2 + 1]))
		buf[off + 124] = f["lm_col"]
		buf[off + 125] = f["lm_row"]
		buf[off + 126] = 0
		buf[off + 127] = 0

	for i, idx in enumerate(order):
		struct.pack_into(">H", buf, order_ofs + i * 2, idx)

	buf[atlas_ofs : atlas_ofs + atlas_bytes] = atlas

	out_path.parent.mkdir(parents=True, exist_ok=True)
	out_path.write_bytes(buf)
	return {
		"map": stem,
		"eyes": len(eyes),
		"faces": len(faces_out),
		"bytes": len(buf),
		"path": str(out_path),
	}


def build_capfaces_packs(
	source: Path,
	output: Path,
	cache: Path,
	map_stems: set[str] | None = None,
	spawns_path: Path | None = None,
) -> list[tuple[str, Path]]:
	"""Write media/cf_<map>.gcp for each BSP (or filtered stems)."""
	cache.mkdir(parents=True, exist_ok=True)
	spawns = None
	if spawns_path and spawns_path.is_file():
		spawns = json.loads(spawns_path.read_text())
	else:
		# try dod-gc default
		for cand in (
			Path("/home/tim/Desktop/dod-gc/playtests/map-spawn-atlas/spawns.json"),
			source.parent.parent / "playtests/map-spawn-atlas/spawns.json",
		):
			if cand.is_file():
				spawns = json.loads(cand.read_text())
				break

	overlays = []
	media = output / "media"
	media.mkdir(parents=True, exist_ok=True)
	maps_dir = source / "maps"
	if not maps_dir.is_dir():
		return overlays
	for bsp in sorted(maps_dir.glob("*.bsp")):
		stem = bsp.stem.lower()
		if map_stems is not None and stem not in map_stems:
			continue
		rel = f"media/cf_{stem}.gcp"
		dest = output / rel
		# Preserve host-prebaked CapFaces (Source→GX Lost Coast smoke, etc.).
		# GoldSrc re-bake of a stub BSP would emit an empty/near-empty pack.
		prebaked = source / rel
		if (
			prebaked.is_file()
			and prebaked.stat().st_size >= 40_000
			and prebaked.read_bytes()[:4] == GC_CF_MAGIC
		):
			dest.parent.mkdir(parents=True, exist_ok=True)
			dest.write_bytes(prebaked.read_bytes())
			print(f"  CapFaces pack {stem}: preserve prebaked {dest.stat().st_size} bytes")
			overlays.append((rel, dest))
			continue
		# cache by bsp sha + bake recipe (spatial coverage bump)
		digest = hashlib.sha256(bsp.read_bytes() + b"|spatial-v1").hexdigest()[:16]
		cached = cache / f"{stem}-{digest}.gcp"
		if cached.is_file():
			dest.write_bytes(cached.read_bytes())
		else:
			info = bake_map(bsp, spawns, cached)
			dest.write_bytes(cached.read_bytes())
			print(f"  CapFaces pack {stem}: faces={info['faces']} eyes={info['eyes']} {info['bytes']} bytes")
		overlays.append((rel, dest))
	if overlays:
		print(f"  CapFaces packs: {len(overlays)} maps")
	return overlays


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--source", type=Path, required=True, help="gamedir with maps/")
	ap.add_argument("--output", type=Path, required=True, help="output tree (writes media/)")
	ap.add_argument("--cache", type=Path, default=None)
	ap.add_argument("--spawns", type=Path, default=None)
	ap.add_argument("--maps", nargs="*", default=None, help="optional map stem filter")
	args = ap.parse_args()
	cache = args.cache or (args.output / "build" / "gc-cf-cache")
	stems = set(m.lower() for m in args.maps) if args.maps else None
	build_capfaces_packs(args.source, args.output, cache, stems, args.spawns)
	return 0


if __name__ == "__main__":
	sys.exit(main())
