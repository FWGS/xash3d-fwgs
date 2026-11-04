#!/usr/bin/env python3
"""Build a Half-Life campaign PDF: per-map entity + BSP lump budget pie charts.

Uses retail map BSPs under Half-Life/valve/maps, optional runtime peaks from a
campaign-audit map-results.tsv, and in-game loading TGA / WAD artwork.
"""

from __future__ import annotations

import argparse
import colorsys
import io
import re
import struct
from collections import Counter, OrderedDict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch  # noqa: E402
from PIL import Image  # noqa: E402
from reportlab.lib.pagesizes import letter  # noqa: E402
from reportlab.lib.units import inch  # noqa: E402
from reportlab.lib.utils import ImageReader  # noqa: E402
from reportlab.pdfgen import canvas  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]

CHAPTERS: list[tuple[str, str, str]] = [
	("Black Mesa Inbound", "c0a0e", "c0a0 c0a0a c0a0b c0a0c c0a0d c0a0e"),
	("Anomalous Materials", "c1a0", "c1a0 c1a0a c1a0b c1a0c c1a0d c1a0e"),
	("Unforeseen Consequences", "c1a1", "c1a1 c1a1a c1a1b c1a1c c1a1d c1a1f"),
	("Office Complex", "c1a2", "c1a2 c1a2a c1a2b c1a2c c1a2d"),
	("We've Got Hostiles", "c1a3", "c1a3 c1a3a c1a3b c1a3c c1a3d"),
	("Blast Pit", "c1a4", "c1a4 c1a4b c1a4d c1a4e c1a4f c1a4g c1a4i c1a4j c1a4k"),
	("Power Up", "c2a1", "c2a1 c2a1a c2a1b"),
	("On A Rail", "c2a2", "c2a2 c2a2a c2a2b1 c2a2b2 c2a2c c2a2d c2a2e c2a2f c2a2g c2a2h"),
	("Apprehension", "c2a3", "c2a3 c2a3a c2a3b c2a3c c2a3d c2a3e"),
	("Residue Processing", "c2a4", "c2a4 c2a4a c2a4b c2a4c c2a4d"),
	("Questionable Ethics", "c2a4e", "c2a4e c2a4f c2a4g"),
	("Surface Tension", "c2a5", "c2a5 c2a5a c2a5b c2a5c c2a5d c2a5e c2a5f c2a5g c2a5w c2a5x"),
	("Forget About Freeman", "c3a1", "c3a1 c3a1a c3a1b"),
	("Lambda Core", "c3a2", "c3a2 c3a2a c3a2b c3a2c c3a2d c3a2e c3a2f"),
	("Xen", "c4a1", "c4a1"),
	("Gonarch's Lair", "c4a2", "c4a2 c4a2a c4a2b"),
	("Interloper", "c4a1a", "c4a1a c4a1b c4a1c c4a1d c4a1e c4a1f"),
	("Nihilanth", "c4a3", "c4a3"),
	("Endgame", "c5a1", "c5a1"),
]

BSP_LUMPS = [
	"entities",
	"planes",
	"textures",
	"vertices",
	"visibility",
	"nodes",
	"texinfo",
	"faces",
	"lighting",
	"clipnodes",
	"leaves",
	"marksurfaces",
	"edges",
	"surfedges",
	"models",
]

# Gold / orange / teal / blood — Half-Life palette-inspired
HL_COLORS = [
	"#F0B418",  # menu gold
	"#C45C26",  # hazard orange
	"#2F6B4F",  # lambda green
	"#8B1E1E",  # blood
	"#3A6EA5",  # HEV blue
	"#D4C4A8",  # concrete
	"#6B4C9A",  # Xen purple
	"#1F1F1F",  # void
	"#E8E0D0",  # lab white
	"#5C7A3A",  # slime
	"#A63D2F",  # rust
	"#4A90A4",  # water
]


def categorize_classname(classname: str) -> str:
	c = classname.lower()
	if c.startswith("monster_") or c in {"cycler", "cycler_sprite", "speech_monster"}:
		return "Monsters / NPCs"
	if c.startswith(("weapon_", "item_", "ammo_")) or c in {"weaponbox"}:
		return "Weapons / Items"
	if c.startswith("light") or c in {"light", "light_spot", "light_environment"}:
		return "Lights"
	if c.startswith("trigger_") or c.startswith("multisource") or c == "multi_manager":
		return "Triggers / Logic"
	if c.startswith("func_"):
		return "Brush funcs"
	if c.startswith(("path_", "tracktrain", "trackchange", "func_tracktrain", "func_trackchange")):
		return "Paths / Trains"
	if c.startswith(("info_", "player_", "worldspawn", "game_")):
		return "Info / Player / World"
	if c.startswith(("env_", "ambient_", "beam", "spark", "cycler_wreckage")):
		return "Env / FX / Ambient"
	if c.startswith("scripted_") or c.startswith("aiscripted_"):
		return "Scripted sequences"
	return "Other"


def parse_bsp(path: Path) -> dict:
	data = path.read_bytes()
	if len(data) < 4 + 15 * 8:
		raise ValueError(f"BSP too small: {path}")
	version = struct.unpack_from("<i", data, 0)[0]
	lumps = []
	for i in range(15):
		off, length = struct.unpack_from("<ii", data, 4 + i * 8)
		lumps.append((max(0, off), max(0, length)))

	lump_sizes = OrderedDict()
	for name, (off, length) in zip(BSP_LUMPS, lumps):
		lump_sizes[name] = length

	ent_off, ent_len = lumps[0]
	ent_blob = data[ent_off : ent_off + ent_len]
	try:
		ent_text = ent_blob.split(b"\0", 1)[0].decode("latin-1", "replace")
	except Exception:
		ent_text = ""

	classnames: list[str] = []
	for match in re.finditer(r'"classname"\s+"([^"]+)"', ent_text, re.IGNORECASE):
		classnames.append(match.group(1))

	by_class = Counter(classnames)
	by_cat: Counter[str] = Counter()
	for cn, n in by_class.items():
		by_cat[categorize_classname(cn)] += n

	return {
		"path": path,
		"map": path.stem,
		"version": version,
		"filesize": len(data),
		"lump_sizes": lump_sizes,
		"entity_total": len(classnames),
		"by_class": by_class,
		"by_category": by_cat,
	}


def load_runtime_peaks(tsv: Path | None) -> dict[str, dict]:
	out: dict[str, dict] = {}
	if not tsv or not tsv.is_file():
		return out
	lines = tsv.read_text(encoding="utf-8", errors="replace").splitlines()
	if not lines:
		return out
	header = lines[0].split("\t")
	for line in lines[1:]:
		cols = line.split("\t")
		row = {header[i]: cols[i] if i < len(cols) else "" for i in range(len(header))}
		name = row.get("map", "").strip()
		if name:
			out[name] = row
	return out


def load_texture_budget(tsv: Path | None) -> dict[str, dict]:
	out: dict[str, dict] = {}
	if not tsv or not tsv.is_file():
		return out
	lines = tsv.read_text(encoding="utf-8", errors="replace").splitlines()
	if not lines:
		return out
	header = lines[0].split("\t")
	for line in lines[1:]:
		cols = line.split("\t")
		row = {header[i]: cols[i] if i < len(cols) else "" for i in range(len(header))}
		name = row.get("map", "").strip()
		if name:
			out[name] = row
	return out


def extract_wad_lmp(wad: Path, lump_name: str) -> Image.Image | None:
	"""Extract a Quake/HL WAD2/3 indexed lump as RGBA PIL image.

	Supports miptex (typ 0x43) and qpic (typ 0x42 / 66 — gfx.wad LAMBDA).
	"""
	if not wad.is_file():
		return None
	blob = wad.read_bytes()
	if len(blob) < 12:
		return None
	magic, count, diroff = struct.unpack_from("<4sii", blob, 0)
	if magic not in (b"WAD2", b"WAD3") or count < 1:
		return None
	want = lump_name.upper()
	for i in range(count):
		off = diroff + i * 32
		if off + 32 > len(blob):
			break
		filepos, disksize, _size, typ, compression, _pad, name = struct.unpack_from(
			"<iiibbh16s", blob, off
		)
		n = name.split(b"\0", 1)[0].decode("latin-1", "replace").upper()
		if n != want or compression:
			continue
		if filepos < 0 or disksize < 8 or filepos + disksize > len(blob):
			return None
		lump = bytearray(blob[filepos : filepos + disksize])

		def from_indexed(w: int, h: int, pix: bytes, pal: bytes) -> Image.Image:
			img = Image.frombytes("P", (w, h), pix)
			pad = pal[:768] + b"\x00" * max(0, 768 - len(pal[:768]))
			img.putpalette(pad)
			rgba = img.convert("RGBA")
			# index 255 is often transparent in HL pics
			pixels = rgba.load()
			for y in range(h):
				for x in range(w):
					if pix[y * w + x] == 255:
						pixels[x, y] = (0, 0, 0, 0)
			return rgba

		# qpic / typ 66: width, height, pixels, [palette count + 768]
		if typ in (0x42, 66) or len(lump) < 40:
			w, h = struct.unpack_from("<ii", lump, 0)
			if w > 0 and h > 0 and 8 + w * h <= len(lump):
				pix = bytes(lump[8 : 8 + w * h])
				pal_off = 8 + w * h
				if pal_off + 2 + 768 <= len(lump):
					pal = bytes(lump[pal_off + 2 : pal_off + 2 + 768])
				elif pal_off + 768 <= len(lump):
					pal = bytes(lump[pal_off : pal_off + 768])
				else:
					continue
				return from_indexed(w, h, pix, pal)
			continue

		# miptex: name[16], w, h, offsets...
		w, h = struct.unpack_from("<ii", lump, 16)
		if w <= 0 or h <= 0 or w * h > len(lump):
			continue
		off0 = struct.unpack_from("<i", lump, 24)[0]
		if off0 < 0 or off0 + w * h > len(lump):
			continue
		pix = bytes(lump[off0 : off0 + w * h])
		pal_off = off0 + w * h + (w // 2) * (h // 2) + (w // 4) * (h // 4) + (w // 8) * (h // 8)
		if pal_off + 2 + 768 > len(lump):
			continue
		pal = bytes(lump[pal_off + 2 : pal_off + 2 + 768])
		return from_indexed(w, h, pix, pal)
	return None


def chapter_art(chapter: str, bg_dir: Path) -> Image.Image | None:
	"""Pick a retail loading TGA tile for the chapter."""
	# Steam loading tiles are lettered; map chapter index to a tile letter.
	letters = "abcdefghijklmnop"
	idx = 0
	for i, (name, _, _) in enumerate(CHAPTERS):
		if name == chapter:
			idx = i
			break
	letter = letters[idx % len(letters)]
	# Prefer 4:3-ish packs if present; fall back to 21_9_1_*
	candidates = sorted(bg_dir.glob(f"*_{letter}_loading.tga"))
	if not candidates:
		candidates = sorted(bg_dir.glob("*_loading.tga"))
	if not candidates:
		return None
	pick = candidates[min(idx, len(candidates) - 1)]
	try:
		return Image.open(pick).convert("RGBA")
	except Exception:
		return None


def pie_colors(n: int) -> list[str]:
	if n <= len(HL_COLORS):
		return HL_COLORS[:n]
	out = []
	for i in range(n):
		h = (i / max(n, 1)) * 0.85
		r, g, b = colorsys.hsv_to_rgb(h, 0.72, 0.92)
		out.append("#%02X%02X%02X" % (int(r * 255), int(g * 255), int(b * 255)))
	return out


def render_dual_pie(
	title: str,
	entity_labels: list[str],
	entity_sizes: list[int],
	lump_labels: list[str],
	lump_sizes: list[int],
	backdrop: Image.Image | None,
	badge: Image.Image | None,
	out_path: Path,
	subtitle: str = "",
) -> Path:
	fig = plt.figure(figsize=(11.0, 7.2), dpi=140)
	fig.patch.set_facecolor("#0C0C0C")

	# Composite darkened chapter art as a true axes background (avoids figimage
	# clipping holes that left black rectangles over the left pie).
	if backdrop is not None:
		ax_bg = fig.add_axes([0, 0, 1, 1], zorder=0)
		bg = backdrop.convert("RGB")
		ax_bg.imshow(bg, aspect="auto", extent=[0, 1, 0, 1], alpha=0.32)
		ax_bg.set_xlim(0, 1)
		ax_bg.set_ylim(0, 1)
		ax_bg.axis("off")
		# dark veil for contrast
		ax_bg.add_patch(
			FancyBboxPatch(
				(0, 0),
				1,
				1,
				boxstyle="square,pad=0",
				facecolor=(0.04, 0.04, 0.04, 0.72),
				edgecolor="none",
				transform=ax_bg.transAxes,
				zorder=1,
			)
		)

	ax1 = fig.add_axes([0.03, 0.28, 0.46, 0.55], zorder=2)
	ax2 = fig.add_axes([0.51, 0.28, 0.46, 0.55], zorder=2)
	for ax in (ax1, ax2):
		ax.set_facecolor((0, 0, 0, 0.15))

	def draw_pie(ax, labels, sizes, heading):
		if not sizes or sum(sizes) <= 0:
			ax.text(0.5, 0.5, "no data", ha="center", va="center", color="#F0B418", fontsize=14)
			ax.axis("off")
			return
		cols = pie_colors(len(sizes))
		wedges, *_ = ax.pie(
			sizes,
			colors=cols,
			startangle=90,
			wedgeprops=dict(width=0.55, edgecolor="#1A1208", linewidth=1.15),
		)
		ax.set_title(heading, color="#F0B418", fontsize=13, pad=10, fontweight="bold")
		ax.legend(
			wedges,
			[f"{lab} ({sz})" for lab, sz in zip(labels, sizes)],
			loc="upper center",
			bbox_to_anchor=(0.5, -0.06),
			fontsize=7.2,
			frameon=False,
			labelcolor="#E8E0D0",
			ncol=2,
		)

	draw_pie(ax1, entity_labels, entity_sizes, "Entities by role")
	labs, vals = [], []
	for lab, raw in zip(lump_labels, lump_sizes):
		if raw <= 0:
			continue
		labs.append(f"{lab} {raw/1024:.0f}K")
		vals.append(raw)
	draw_pie(ax2, labs, vals, "BSP lump budget (bytes)")

	fig.text(0.5, 0.945, title, ha="center", color="#F0B418", fontsize=18, fontweight="bold", zorder=5)
	if subtitle:
		fig.text(0.5, 0.905, subtitle, ha="center", color="#D4C4A8", fontsize=10, zorder=5)

	if badge is not None:
		axb = fig.add_axes([0.015, 0.88, 0.08, 0.09], zorder=6)
		axb.imshow(badge.convert("RGBA"))
		axb.axis("off")

	out_path.parent.mkdir(parents=True, exist_ok=True)
	fig.savefig(out_path, facecolor=fig.get_facecolor())
	plt.close(fig)
	return out_path


def top_categories(counter: Counter, limit: int = 8) -> tuple[list[str], list[int]]:
	items = counter.most_common()
	if len(items) <= limit:
		return [k for k, _ in items], [v for _, v in items]
	head = items[: limit - 1]
	rest = sum(v for _, v in items[limit - 1 :])
	labels = [k for k, _ in head] + ["Other"]
	sizes = [v for _, v in head] + [rest]
	return labels, sizes


def top_lumps(lump_sizes: OrderedDict, limit: int = 8) -> tuple[list[str], list[int]]:
	items = sorted(lump_sizes.items(), key=lambda kv: kv[1], reverse=True)
	items = [(k, v) for k, v in items if v > 0]
	if len(items) <= limit:
		return [k for k, _ in items], [v for _, v in items]
	head = items[: limit - 1]
	rest = sum(v for _, v in items[limit - 1 :])
	return [k for k, _ in head] + ["other lumps"], [v for _, v in head] + [rest]


def build_pdf(
	maps_dir: Path,
	output: Path,
	workdir: Path,
	runtime: dict[str, dict],
	texbud: dict[str, dict],
	wad: Path,
	bg_dir: Path,
) -> Path:
	workdir.mkdir(parents=True, exist_ok=True)
	output.parent.mkdir(parents=True, exist_ok=True)

	lambda_img = extract_wad_lmp(wad, "LAMBDA")
	if lambda_img is None:
		lambda_img = extract_wad_lmp(wad, "LAMBDA1")

	pages: list[tuple[str, Path, dict]] = []
	chapter_summaries: list[tuple[str, Counter, OrderedDict, int]] = []

	for chapter, _rep, maps in CHAPTERS:
		art = chapter_art(chapter, bg_dir)
		ch_ent: Counter = Counter()
		ch_lumps: OrderedDict = OrderedDict((n, 0) for n in BSP_LUMPS)
		ch_maps = 0
		for map_name in maps.split():
			bsp = maps_dir / f"{map_name}.bsp"
			if not bsp.is_file():
				continue
			info = parse_bsp(bsp)
			ch_maps += 1
			ch_ent.update(info["by_category"])
			for k, v in info["lump_sizes"].items():
				ch_lumps[k] = ch_lumps.get(k, 0) + v

			elabels, esizes = top_categories(info["by_category"])
			llabels, lsizes = top_lumps(info["lump_sizes"])
			rt = runtime.get(map_name, {})
			tb = texbud.get(map_name, {})
			peak = rt.get("memory_peak", "")
			status = rt.get("status", "")
			tex_note = ""
			if tb.get("resident_bytes"):
				try:
					tex_note = f"  |  tex resident {int(tb['resident_bytes'])/1024:.0f} KiB / pool {int(tb.get('pool_bytes') or 0)/1024:.0f} KiB"
				except Exception:
					pass
			sub = f"{info['entity_total']} entities  |  BSP {info['filesize']/1024:.0f} KiB"
			if peak:
				sub += f"  |  runtime peak {peak} ({status or 'n/a'})"
			sub += tex_note

			png = workdir / f"{map_name}.png"
			render_dual_pie(
				title=f"{chapter}  —  {map_name}",
				entity_labels=elabels,
				entity_sizes=esizes,
				lump_labels=llabels,
				lump_sizes=lsizes,
				backdrop=art,
				badge=lambda_img,
				out_path=png,
				subtitle=sub,
			)
			pages.append((chapter, png, info))

		if ch_maps:
			chapter_summaries.append((chapter, ch_ent, ch_lumps, ch_maps))

	# Cover + chapter overview pages via reportlab
	c = canvas.Canvas(str(output), pagesize=letter)
	W, H = letter

	def cover():
		c.setFillColorRGB(0.05, 0.05, 0.05)
		c.rect(0, 0, W, H, fill=1, stroke=0)
		if lambda_img is not None:
			buf = io.BytesIO()
			lambda_img.resize((180, 180), Image.Resampling.NEAREST).save(buf, format="PNG")
			buf.seek(0)
			c.drawImage(ImageReader(buf), W / 2 - 90, H - 280, 180, 180, mask="auto")
		c.setFillColorRGB(0.94, 0.70, 0.09)
		c.setFont("Helvetica-Bold", 26)
		c.drawCentredString(W / 2, H - 320, "Half-Life Campaign")
		c.setFont("Helvetica-Bold", 16)
		c.drawCentredString(W / 2, H - 348, "GameCube port — per-level budget & entities")
		c.setFillColorRGB(0.83, 0.77, 0.66)
		c.setFont("Helvetica", 11)
		c.drawCentredString(W / 2, H - 380, "Pie charts from retail BSP entity lumps + lump byte budgets")
		c.drawCentredString(W / 2, H - 396, "Backdrop tiles from valve/resource/background loading art")
		c.drawCentredString(W / 2, H - 412, "Lambda badge from gfx.wad  |  runtime peaks from campaign audit when present")
		c.setFont("Helvetica", 9)
		c.drawCentredString(W / 2, 48, "Xash3D GameCube  |  output/pdf/campaign-level-budgets.pdf")
		c.showPage()

	cover()

	# Chapter rollup pages (2 chapters per page)
	for i in range(0, len(chapter_summaries), 1):
		chapter, ch_ent, ch_lumps, ch_maps = chapter_summaries[i]
		art = chapter_art(chapter, bg_dir)
		elabels, esizes = top_categories(ch_ent)
		llabels, lsizes = top_lumps(ch_lumps)
		png = workdir / f"chapter_{i:02d}.png"
		render_dual_pie(
			title=f"Chapter: {chapter}",
			entity_labels=elabels,
			entity_sizes=esizes,
			lump_labels=llabels,
			lump_sizes=lsizes,
			backdrop=art,
			badge=lambda_img,
			out_path=png,
			subtitle=f"Rollup across {ch_maps} map(s)  |  {sum(ch_ent.values())} entity records",
		)
		c.setFillColorRGB(0.05, 0.05, 0.05)
		c.rect(0, 0, W, H, fill=1, stroke=0)
		c.drawImage(str(png), 0.35 * inch, 0.55 * inch, width=W - 0.7 * inch, height=H - 1.0 * inch, preserveAspectRatio=True, anchor="c")
		c.setFillColorRGB(0.7, 0.7, 0.7)
		c.setFont("Helvetica", 8)
		c.drawString(0.5 * inch, 0.35 * inch, f"Chapter overview  {i+1}/{len(chapter_summaries)}")
		c.showPage()

	# Per-map pages
	for idx, (chapter, png, info) in enumerate(pages):
		c.setFillColorRGB(0.05, 0.05, 0.05)
		c.rect(0, 0, W, H, fill=1, stroke=0)
		c.drawImage(str(png), 0.3 * inch, 0.5 * inch, width=W - 0.6 * inch, height=H - 0.9 * inch, preserveAspectRatio=True, anchor="c")
		c.setFillColorRGB(0.65, 0.65, 0.65)
		c.setFont("Helvetica", 8)
		c.drawString(0.45 * inch, 0.32 * inch, f"{info['map']}  page {idx+1}/{len(pages)}")
		c.showPage()

	c.save()
	return output


def latest_audit_tsv(root: Path) -> tuple[Path | None, Path | None]:
	logs = root / ".ai" / "logs"
	if not logs.is_dir():
		return None, None
	audits = sorted(logs.glob("campaign-audit-*"), key=lambda p: p.name, reverse=True)
	for d in audits:
		m = d / "map-results.tsv"
		t = d / "texture-budget-results.tsv"
		if m.is_file():
			return m, t if t.is_file() else None
	return None, None


def main() -> None:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--maps", type=Path, default=ROOT / "Half-Life" / "valve" / "maps")
	ap.add_argument("--wad", type=Path, default=ROOT / "Half-Life" / "valve" / "gfx.wad")
	ap.add_argument(
		"--backgrounds",
		type=Path,
		default=ROOT / "Half-Life" / "valve" / "resource" / "background",
	)
	ap.add_argument("--output", type=Path, default=ROOT / "output" / "pdf" / "campaign-level-budgets.pdf")
	ap.add_argument("--workdir", type=Path, default=ROOT / "tmp" / "pdfs" / "campaign-charts")
	ap.add_argument("--runtime-tsv", type=Path, default=None)
	ap.add_argument("--texture-tsv", type=Path, default=None)
	args = ap.parse_args()

	rt_tsv, tex_tsv = args.runtime_tsv, args.texture_tsv
	if rt_tsv is None:
		rt_tsv, auto_tex = latest_audit_tsv(ROOT)
		if tex_tsv is None:
			tex_tsv = auto_tex

	runtime = load_runtime_peaks(rt_tsv)
	texbud = load_texture_budget(tex_tsv)
	print(f"maps={args.maps}")
	print(f"runtime_tsv={rt_tsv} rows={len(runtime)}")
	print(f"texture_tsv={tex_tsv} rows={len(texbud)}")

	pdf = build_pdf(
		args.maps,
		args.output,
		args.workdir,
		runtime,
		texbud,
		args.wad,
		args.backgrounds,
	)
	print(f"wrote {pdf}")


if __name__ == "__main__":
	main()
