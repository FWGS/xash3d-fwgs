#!/usr/bin/env python3
"""Generate a safe GameCube BSP lump index without rewriting the BSP."""
from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

LUMP_NAMES = (
    "entities", "planes", "textures", "vertexes", "visibility", "nodes",
    "texinfo", "faces", "lighting", "clipnodes", "leafs", "marksurfaces",
    "edges", "surfedges", "models",
)
HOT = {"planes", "visibility", "nodes", "texinfo", "faces", "leafs", "marksurfaces", "models"}

def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {argv[0]} INPUT.BSP OUTPUT.GCMETA", file=sys.stderr)
        return 2
    source, output = map(Path, argv[1:])
    data = source.read_bytes()
    if len(data) < 4 + 15 * 8:
        raise SystemExit(f"BSP is too small: {source}")
    version = struct.unpack_from("<i", data, 0)[0]
    rows = [f"GCMETA 1 {version} {zlib.crc32(data) & 0xffffffff:08x} {len(data)}\n"]
    ranges: list[tuple[int, int, str]] = []
    for index, name in enumerate(LUMP_NAMES):
        offset, size = struct.unpack_from("<ii", data, 4 + index * 8)
        if offset < 0 or size < 0 or offset + size > len(data):
            raise SystemExit(f"invalid {name} lump in {source}")
        if size:
            ranges.append((offset, offset + size, name))
        tier = "HOT" if name in HOT else "COLD"
        rows.append(f"{index:02d} {name} {tier} {offset} {size} {offset & 31}\n")
    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if current[0] < previous[1]:
            raise SystemExit(f"overlapping BSP lumps: {previous[2]} and {current[2]}")
    # GoldSrc dface_t stores texinfo at byte 10; group faces by texinfo so
    # the renderer can later submit compatible materials in contiguous runs.
    face_ofs, face_len = struct.unpack_from("<ii", data, 4 + 7 * 8)
    tex_ofs, tex_len = struct.unpack_from("<ii", data, 4 + 6 * 8)
    if face_len % 20 or tex_len % 40:
        raise SystemExit(f"invalid face/texinfo stride in {source}")
    face_count = face_len // 20
    tex_count = tex_len // 40
    batches: dict[tuple[int, int], list[int]] = {}
    for face_index in range(face_count):
        texinfo = struct.unpack_from("<h", data, face_ofs + face_index * 20 + 10)[0]
        if texinfo < 0 or texinfo >= tex_count:
            raise SystemExit(f"invalid texinfo index {texinfo} in face {face_index}")
        miptex, flags = struct.unpack_from("<ii", data, tex_ofs + texinfo * 40 + 32)
        batches.setdefault((miptex, flags), []).append(face_index)
    rows.append(f"BATCHES {len(batches)} {face_count}\n")
    for (miptex, flags), faces in sorted(batches.items()):
        rows.append("BATCH %d %d %d %d %s\n" % (
            miptex, flags, len(faces), faces[0], ",".join(str(face) for face in faces)))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("".join(rows), encoding="ascii")
    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
