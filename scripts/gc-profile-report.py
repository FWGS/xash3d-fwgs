#!/usr/bin/env python3
"""Summarise -gcprofile output of a Dolphin probe log, per map.

The engine prints a block every 64 frames:
  profile frames=64 avg_frame=35.96ms state=0
  profile stage="host frame" avg=0.67ms per_frame=0.67ms hits=64
  ...
This groups the blocks by the map that was active (map loaded / changelevel
lines), and prints per map: blocks, mean and worst avg_frame, and the
stages with the largest mean per-frame cost.

  scripts/gc-profile-report.py .ai/logs/dolphin-probe-<stamp> [--top 15] [--markdown]
"""

import argparse
import collections
import os
import re
import sys

FRAME_RE = re.compile(r"profile frames=(\d+) avg_frame=([\d.]+)ms")
STAGE_RE = re.compile(r'profile stage="([^"]+)" avg=([\d.]+)ms per_frame=([\d.]+)ms hits=(\d+)')
MAP_RES = (
	re.compile(r"SV_ChangeLevel spawn ready map=(\S+)"),
	re.compile(r"Xash3D GameCube: map loaded (\S+)"),
	re.compile(r"mem stage=map active .* map=(\S+)"),
)


def parse(path):
	maps = collections.OrderedDict()
	current = "(boot)"
	block = None
	with open(path, "rb") as f:
		for raw in f:
			line = raw.decode("latin-1", "replace")
			for rx in MAP_RES:
				m = rx.search(line)
				if m:
					current = m.group(1)
					break
			m = FRAME_RE.search(line)
			if m:
				block = {"frame": float(m.group(2)), "stages": {}}
				maps.setdefault(current, []).append(block)
				continue
			m = STAGE_RE.search(line)
			if m and block is not None:
				block["stages"][m.group(1)] = float(m.group(3))
	return maps


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("logdir")
	ap.add_argument("--top", type=int, default=15)
	ap.add_argument("--markdown", action="store_true")
	args = ap.parse_args()
	path = args.logdir
	if os.path.isdir(path):
		path = os.path.join(path, "stderr.log")
	maps = parse(path)
	if not maps:
		sys.exit("no profile blocks (was the disc built with -gcprofile?)")
	rows = []
	for name, blocks in maps.items():
		frames = [b["frame"] for b in blocks]
		mean = sum(frames) / len(frames)
		stages = collections.defaultdict(float)
		for b in blocks:
			for k, v in b["stages"].items():
				stages[k] += v / len(blocks)
		top = sorted(stages.items(), key=lambda kv: -kv[1])[: args.top]
		ordered = sorted(frames)
		p95 = ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))]
		first, spent = [], 0.0
		for f in frames:  # blocks in the first 15 s of the map
			if spent >= 15000.0:
				break
			first.append(f)
			spent += f * 64
		first15 = sum(first) / len(first)
		rows.append(f"| {name} | {mean:.1f} | {1000 / mean:.0f} | {p95:.1f} | {max(frames):.1f} | {first15:.1f} |")
		if args.markdown:
			print(f"\n### {name}: {len(blocks)} x 64 frames, mean {mean:.1f} ms ({1000 / mean:.0f} fps), worst block {max(frames):.1f} ms\n")
			print("| Stage | ms/frame |\n|---|---|")
			for k, v in top:
				print(f"| {k} | {v:.2f} |")
		else:
			print(f"== {name}: blocks={len(blocks)} mean={mean:.1f}ms ({1000 / mean:.0f} fps) worst={max(frames):.1f}ms")
			for k, v in top:
				print(f"   {v:7.2f}  {k}")
	print("\n| Map | mean ms | fps | p95 ms | worst ms | first 15 s ms |\n|---|---|---|---|---|---|")
	print("\n".join(rows))
	return 0


if __name__ == "__main__":
	sys.exit(main())
