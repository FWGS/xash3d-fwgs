#!/usr/bin/env python3
"""Big always-on-top MUTED / SOUND ON toggle for scripts/play-dolphin.sh.

Only what reaches the speakers is silenced: the game keeps mixing audio and
Dolphin keeps running at full speed. Two ways, picked by the launcher:

  --pid P                 mute Dolphin's own output stream (no recording)
  --game-sink S [--speaker-sink O]
                          recording: Dolphin plays into the private sink S
                          (recorded from S.monitor); sound on = a loopback
                          S.monitor -> speakers exists, muted = no loopback.
                          (PipeWire undoes a mute set on a loopback stream.)

  --muted / --unmuted     start state
  --no-window             keep the start state while Dolphin runs, no window
  --once                  with --no-window: apply once and exit
  --rec-pid R             show REC while the recorder process R runs

SIGUSR1 toggles (e.g. a desktop shortcut: pkill -USR1 -f dolphin-mute-toggle).
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import time

MUTED_BG, SOUND_BG, WAIT_BG = "#b3261e", "#1e7b34", "#555555"


def pactl(*args):
	return subprocess.run(["pactl", *args], capture_output=True, text=True, timeout=5)


def alive(pid):
	if not pid:
		return True
	try:
		os.kill(pid, 0)
		return True
	except OSError:
		return False


class StreamMute:
	"""Mute Dolphin's own sink-input(s)."""

	def __init__(self, pid):
		self.pid = pid

	def streams(self):
		try:
			inputs = json.loads(pactl("-f", "json", "list", "sink-inputs").stdout or "[]")
		except (OSError, ValueError, subprocess.SubprocessError):
			return []
		found = []
		for si in inputs:
			props = si.get("properties", {})
			if ( self.pid and props.get("application.process.id") == str(self.pid)) or \
					( not self.pid and props.get("application.name") == "Dolphin Emulator"):
				found.append((si["index"], si.get("mute", False)))
		return found

	def muted(self):
		s = self.streams()
		return None if not s else all(m for _, m in s)

	def set(self, muted):
		s = self.streams()
		for index, _ in s:
			pactl("set-sink-input-mute", str(index), "1" if muted else "0")
		return bool(s)

	def close(self):
		pass


class LoopbackMute:
	"""Sound on = a loopback from the game sink's monitor to the speakers.

	The state is read back from the server (loopback modules whose source is
	the game sink's monitor), never kept locally: a slow or timed-out pactl
	call must not leave the window showing the opposite of what is heard."""

	def __init__(self, game_sink, speaker_sink):
		self.game_sink, self.speaker_sink = game_sink, speaker_sink

	def modules(self):
		try:
			out = pactl("list", "short", "modules").stdout
		except (OSError, subprocess.SubprocessError):
			return None
		source = f"source={self.game_sink}.monitor"
		return [line.split()[0] for line in out.splitlines()
			if "\tmodule-loopback\t" in line and source in line.split()]

	def muted(self):
		mods = self.modules()
		return None if mods is None else not mods

	def set(self, muted):
		mods = self.modules()
		if mods is None:
			return False
		if muted:
			for m in mods:
				pactl("unload-module", m)
		elif not mods:
			extra = [f"sink={self.speaker_sink}"] if self.speaker_sink else []
			try:
				pactl("load-module", "module-loopback", f"source={self.game_sink}.monitor",
					"latency_msec=60", *extra)
			except subprocess.SubprocessError:
				pass
		return True

	def close(self):
		self.set(True)


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--pid", type=int, help="Dolphin process id")
	ap.add_argument("--game-sink", help="private sink Dolphin plays into (recording)")
	ap.add_argument("--speaker-sink", help="with --game-sink: output to play on (default output)")
	group = ap.add_mutually_exclusive_group()
	group.add_argument("--muted", action="store_true", help="start muted")
	group.add_argument("--unmuted", action="store_true", help="start with sound on")
	ap.add_argument("--no-window", action="store_true", help="keep the start state while Dolphin runs")
	ap.add_argument("--once", action="store_true", help="with --no-window, apply once and exit")
	ap.add_argument("--rec-pid", type=int, help="show REC while this recorder process runs")
	args = ap.parse_args()

	ctl = LoopbackMute(args.game_sink, args.speaker_sink) if args.game_sink else StreamMute(args.pid)
	# Closed by the launcher: still remove the loopback (finally below).
	signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
	want = {"muted": True if args.muted else False if args.unmuted else None}

	def enforce():
		# Dolphin opens its stream when emulation starts (and may reopen it):
		# keep the chosen state applied.
		if want["muted"] is not None and ctl.muted() not in (None, want["muted"]):
			ctl.set(want["muted"])
		elif want["muted"] is not None and ctl.muted() is None:
			ctl.set(want["muted"])

	try:
		if args.no_window:
			enforce()
			while not args.once and args.pid and alive(args.pid):
				enforce()
				time.sleep(0.5)
			return 0

		import tkinter as tk

		root = tk.Tk()
		root.title("Xash3D GC sound")
		root.attributes("-topmost", True)
		root.resizable(False, False)
		rec = tk.Label(root, text="", font=("Sans", 14, "bold"), fg="#d00000")
		rec.pack(side="top", pady=(6, 0))
		button = tk.Button(root, font=("Sans", 28, "bold"), fg="white", width=11, height=2,
			relief="flat", bd=0, activeforeground="white")
		button.pack(padx=8, pady=8)
		tk.Label(root, text="click, Space or M", font=("Sans", 10)).pack(side="bottom", pady=(0, 6))

		def render():
			m = ctl.muted()
			text, bg = ("waiting…", WAIT_BG) if m is None else ("MUTED", MUTED_BG) if m else ("SOUND ON", SOUND_BG)
			button.configure(text=text, bg=bg, activebackground=bg)
			if args.rec_pid:
				rec.configure(text="● REC  (recording with sound)" if alive(args.rec_pid) else "recording stopped")

		def toggle(_event=None):
			m = ctl.muted()
			if m is not None:
				want["muted"] = not m
				ctl.set(want["muted"])
			render()

		def tick():
			if not alive(args.pid):
				root.destroy()
				return
			enforce()
			render()
			root.after(500, tick)

		button.configure(command=toggle)
		for key in ("<space>", "m", "M"):
			root.bind(key, toggle)
		signal.signal(signal.SIGUSR1, lambda *_: root.after(0, toggle))
		enforce()
		render()
		root.after(500, tick)
		root.mainloop()
	finally:
		ctl.close()
	return 0


if __name__ == "__main__":
	sys.exit(main())
