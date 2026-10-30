#!/usr/bin/env python3
"""Big always-on-top MUTED / SOUND ON toggle for scripts/play-dolphin.sh.

Mutes Dolphin's own output stream in PulseAudio/PipeWire (pactl), so only
what reaches the speakers is silenced: the game keeps mixing audio and
Dolphin keeps running at full speed. The button shows the stream's real
mute state, re-read every half second.

  --muted        mute Dolphin's stream as soon as it appears
  --no-window    apply --muted / --unmuted and exit (no window)
"""

import argparse
import json
import os
import subprocess
import sys
import time

MUTED_BG, SOUND_BG, WAIT_BG = "#b3261e", "#1e7b34", "#555555"


def dolphin_streams(pid):
	"""Sink-input indices and mute flags of the Dolphin process `pid`."""
	try:
		out = subprocess.run(["pactl", "-f", "json", "list", "sink-inputs"],
			capture_output=True, text=True, timeout=3).stdout
		inputs = json.loads(out or "[]")
	except (OSError, ValueError, subprocess.SubprocessError):
		return []
	found = []
	for si in inputs:
		props = si.get("properties", {})
		if pid and props.get("application.process.id") == str(pid):
			found.append((si["index"], si.get("mute", False)))
		elif not pid and props.get("application.name") == "Dolphin Emulator":
			found.append((si["index"], si.get("mute", False)))
	return found


def set_mute(pid, muted):
	streams = dolphin_streams(pid)
	for index, _ in streams:
		subprocess.run(["pactl", "set-sink-input-mute", str(index), "1" if muted else "0"],
			capture_output=True, timeout=3)
	return bool(streams)


def alive(pid):
	if not pid:
		return True
	try:
		os.kill(pid, 0)
		return True
	except OSError:
		return False


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--pid", type=int, help="Dolphin process id (default: any Dolphin stream)")
	group = ap.add_mutually_exclusive_group()
	group.add_argument("--muted", action="store_true", help="start muted")
	group.add_argument("--unmuted", action="store_true", help="start with sound on")
	ap.add_argument("--no-window", action="store_true", help="apply the start state and exit")
	args = ap.parse_args()

	# Dolphin opens its stream when emulation starts: apply the start state as
	# soon as it shows up, before the game makes any sound.
	want = { "muted": True if args.muted else False if args.unmuted else None }
	deadline = time.time() + 60
	if want["muted"] is not None:
		while alive(args.pid) and time.time() < deadline and not set_mute(args.pid, want["muted"]):
			time.sleep(0.1)
	if args.no_window:
		return 0

	import tkinter as tk

	root = tk.Tk()
	root.title("Xash3D GC sound")
	root.attributes("-topmost", True)
	root.resizable(False, False)
	button = tk.Button(root, font=("Sans", 28, "bold"), fg="white", width=11, height=2,
		relief="flat", bd=0, activeforeground="white")
	tk.Label(root, text="click, Space or M", font=("Sans", 10)).pack(side="bottom", pady=(0, 6))
	button.pack(padx=8, pady=8)

	def render():
		streams = dolphin_streams(args.pid)
		if not streams:
			text, bg = "waiting…", WAIT_BG
		elif all(m for _, m in streams):
			text, bg = "MUTED", MUTED_BG
		else:
			text, bg = "SOUND ON", SOUND_BG
		button.configure(text=text, bg=bg, activebackground=bg)
		return streams

	def toggle(_event=None):
		streams = dolphin_streams(args.pid)
		if streams:
			set_mute(args.pid, not all(m for _, m in streams))
		render()

	def tick():
		if not alive(args.pid):
			root.destroy()
			return
		render()
		root.after(500, tick)

	button.configure(command=toggle)
	root.bind("<space>", toggle)
	root.bind("m", toggle)
	root.bind("M", toggle)
	render()
	root.after(500, tick)
	root.mainloop()
	return 0


if __name__ == "__main__":
	sys.exit(main())
