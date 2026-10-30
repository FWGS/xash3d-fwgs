#!/usr/bin/env python3
"""Record a Dolphin playtest (video + the game's audio) to .ai/playtests/.

Audio: Dolphin plays into a private PulseAudio/PipeWire null sink (set up by
the caller with `sink` below, PULSE_SINK=<name>); this records that sink's
monitor, i.e. the game's real mix, whatever the speakers are doing (the
launcher's mute only silences the loopback to the speakers).
Video: the Dolphin render window via ffmpeg x11grab (no guest impact).

Written as crash-safe Matroska while recording, then remuxed to MP4 (no
re-encode) with a JSON sidecar, and .ai/playtests/INDEX.md is regenerated.

  playtest-record.py sink create NAME [--loopback]   -> prints module ids
  playtest-record.py sink destroy MODULE_ID...
  playtest-record.py record --pid PID --sink NAME --route R [--disc ISO] [--logdir DIR]
  playtest-record.py annotate FILE.json key=value...
  playtest-record.py index
"""

import argparse
import datetime
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, ".ai", "playtests")
MIN_FREE_GB = 5.0


def sh(*cmd, **kw):
	return subprocess.run(cmd, capture_output=True, text=True, **kw)


def sink_create(name, loopback, loopback_sink=None):
	mods = []
	r = sh("pactl", "load-module", "module-null-sink", f"sink_name={name}",
		f"sink_properties=device.description={name}")
	if r.returncode != 0:
		sys.exit(f"null sink failed: {r.stderr.strip()}")
	mods.append(r.stdout.strip())
	if loopback:
		extra = [f"sink={loopback_sink}"] if loopback_sink else []
		r = sh("pactl", "load-module", "module-loopback", f"source={name}.monitor", "latency_msec=60", *extra)
		if r.returncode == 0:
			mods.append(r.stdout.strip())
	print(" ".join(mods))


def sink_destroy(mods):
	mods = " ".join(mods).split()  # ids may arrive as one "a b" argument
	for m in reversed(mods):
		sh("pactl", "unload-module", m)


def find_render_window(timeout):
	"""The Dolphin render window (titled GameCube), not the 800x600 main one."""
	deadline = time.time() + timeout
	while time.time() < deadline:
		tree = sh("xwininfo", "-root", "-tree").stdout
		for line in tree.splitlines():
			if '("dolphin-emu" "GameCube")' not in line or ")  800x600" in line:
				continue
			m = re.search(r"(\d+)x(\d+)\+", line)
			if m and int(m.group(1)) >= 300 and int(m.group(2)) >= 200:
				return line.split()[0]
		time.sleep(0.5)
	return None


def window_exists(win):
	return sh("xwininfo", "-id", win).returncode == 0


def alive(pid):
	if not pid:
		return True
	try:
		os.kill(pid, 0)
		return True
	except OSError:
		return False


def git_sha():
	r = sh("git", "-C", ROOT, "rev-parse", "--short=10", "HEAD")
	return r.stdout.strip() or "unknown"


def probe_media(path):
	r = sh("ffprobe", "-v", "error", "-show_entries", "format=duration:stream=codec_type,codec_name",
		"-of", "json", path)
	try:
		d = json.loads(r.stdout)
	except ValueError:
		return 0.0, []
	return float(d.get("format", {}).get("duration", 0) or 0), \
		[f'{s.get("codec_type")}:{s.get("codec_name")}' for s in d.get("streams", [])]


def audio_levels(path):
	r = sh("ffmpeg", "-hide_banner", "-nostats", "-i", path, "-vn", "-af", "volumedetect", "-f", "null", "-")
	mean = re.search(r"mean_volume: (-?[\d.inf]+) dB", r.stderr)
	peak = re.search(r"max_volume: (-?[\d.inf]+) dB", r.stderr)
	return (mean.group(1) if mean else None), (peak.group(1) if peak else None)


def record(args):
	os.makedirs(OUT_DIR, exist_ok=True)
	free_gb = shutil.disk_usage(OUT_DIR).free / 1e9
	if free_gb < MIN_FREE_GB:
		print(f"playtest-record: only {free_gb:.1f} GB free (< {MIN_FREE_GB}), not recording", file=sys.stderr)
		return 0
	win = find_render_window(args.window_timeout)
	if not win or not alive(args.pid):
		print("playtest-record: no Dolphin render window, not recording", file=sys.stderr)
		return 0

	stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
	sha = git_sha()
	base = os.path.join(OUT_DIR, f"{stamp}-{args.route}-{sha}")
	mkv = base + ".mkv"
	ff = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
		"-thread_queue_size", "512", "-f", "x11grab", "-draw_mouse", "0", "-framerate", str(args.fps), "-window_id", win, "-i", os.environ.get("DISPLAY", ":0"),
		"-thread_queue_size", "512", "-f", "pulse", "-i", f"{args.sink}.monitor",
		"-c:v", "libx264", "-preset", "veryfast", "-crf", "30", "-pix_fmt", "yuv420p", "-g", str(args.fps * 2),
		"-c:a", "aac", "-b:a", "96k", "-f", "matroska", mkv],
		stdin=subprocess.PIPE, stderr=open(base + ".ffmpeg.log", "w"))
	started = time.time()
	stop = {"now": False}
	signal.signal(signal.SIGTERM, lambda *_: stop.__setitem__("now", True))
	signal.signal(signal.SIGINT, lambda *_: stop.__setitem__("now", True))
	# --pid 0: record until the render window goes away.
	while alive(args.pid) and ff.poll() is None and not stop["now"] \
			and ( args.pid or window_exists( win )):
		time.sleep(0.5)
	if ff.poll() is None:
		try:
			ff.communicate(b"q", timeout=20)
		except subprocess.TimeoutExpired:
			ff.send_signal(signal.SIGINT)
			ff.wait(timeout=10)

	out = base + ".mp4"
	if sh("ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", mkv, "-c", "copy",
			"-movflags", "+faststart", out).returncode == 0 and os.path.getsize(out) > 0:
		os.remove(mkv)
		# Only x11grab's "window closed" note ends up here on a good run.
		if os.path.exists(base + ".ffmpeg.log"):
			os.remove(base + ".ffmpeg.log")
	else:
		out = mkv  # keep the Matroska file if the remux failed
	ffl = base + ".ffmpeg.log"
	if os.path.exists(ffl) and os.path.getsize(ffl) == 0:
		os.remove(ffl)

	duration, streams = probe_media(out)
	mean_db, peak_db = audio_levels(out)
	meta = {
		"file": os.path.basename(out),
		"date": datetime.datetime.fromtimestamp(started).isoformat(timespec="seconds"),
		"commit": sha,
		"route": args.route,
		"disc": args.disc,
		"log_dir": args.logdir,
		"duration_s": round(duration, 1),
		"streams": streams,
		"audio_mean_db": mean_db,
		"audio_peak_db": peak_db,
		"size_mb": round(os.path.getsize(out) / 1e6, 1),
		"mb_per_min": round(os.path.getsize(out) / 1e6 / max(duration / 60.0, 1e-6), 1),
		"keep": False,
	}
	with open(base + ".json", "w") as f:
		json.dump(meta, f, indent=1)
	write_index()
	print(base + ".json")
	if args.json_out:
		with open(args.json_out, "w") as f:
			f.write(base + ".json\n")
	return 0


def annotate(path, pairs):
	with open(path) as f:
		meta = json.load(f)
	for p in pairs:
		k, _, v = p.partition("=")
		meta[k] = {"true": True, "false": False}.get(v, v)
	with open(path, "w") as f:
		json.dump(meta, f, indent=1)
	write_index()


def write_index():
	rows = []
	for name in sorted(os.listdir(OUT_DIR), reverse=True):
		if not name.endswith(".json"):
			continue
		try:
			with open(os.path.join(OUT_DIR, name)) as f:
				m = json.load(f)
		except (OSError, ValueError):
			continue
		rows.append(m)
	lines = ["# Playtest recordings", "",
		"Newest first. Regenerated by scripts/playtest-record.py; prune with",
		"scripts/prune-playtests.sh (keeps the newest 20 and anything with keep=true).", "",
		"| Date | Recording | Commit | Route | Length | Result | Audio peak | Size | Keep |",
		"|---|---|---|---|---|---|---|---|---|"]
	for m in rows:
		mins, secs = divmod(int(m.get("duration_s", 0)), 60)
		lines.append("| {} | [{}]({}) | {} | {} | {}:{:02d} | {} | {} dB | {} MB | {} |".format(
			m.get("date", "?").replace("T", " "), m.get("file"), m.get("file"), m.get("commit"),
			m.get("route"), mins, secs, m.get("result", "-"), m.get("audio_peak_db", "?"),
			m.get("size_mb", "?"), "yes" if m.get("keep") else ""))
	with open(os.path.join(OUT_DIR, "INDEX.md"), "w") as f:
		f.write("\n".join(lines) + "\n")


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = ap.add_subparsers(dest="cmd", required=True)
	s = sub.add_parser("sink")
	s.add_argument("action", choices=["create", "destroy"])
	s.add_argument("args", nargs="*")
	s.add_argument("--loopback", action="store_true", help="also play the sink on the default output")
	s.add_argument("--loopback-sink", help="play it on this sink instead of the default output")
	r = sub.add_parser("record")
	r.add_argument("--pid", type=int, required=True, help="Dolphin pid; 0 = until the window closes")
	r.add_argument("--sink", required=True)
	r.add_argument("--route", default="play")
	r.add_argument("--disc", default="")
	r.add_argument("--logdir", default="")
	r.add_argument("--fps", type=int, default=30)
	r.add_argument("--window-timeout", type=float, default=180)
	r.add_argument("--json-out", help="write the sidecar path here when done")
	a = sub.add_parser("annotate")
	a.add_argument("json")
	a.add_argument("pairs", nargs="*")
	sub.add_parser("index")
	args = ap.parse_args()

	if args.cmd == "sink":
		if args.action == "create":
			sink_create(args.args[0], args.loopback, args.loopback_sink)
		else:
			sink_destroy(args.args)
	elif args.cmd == "record":
		return record(args)
	elif args.cmd == "annotate":
		annotate(args.json, args.pairs)
	else:
		os.makedirs(OUT_DIR, exist_ok=True)
		write_index()
	return 0


if __name__ == "__main__":
	sys.exit(main())
