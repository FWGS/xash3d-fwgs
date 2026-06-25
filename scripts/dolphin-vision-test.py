#!/usr/bin/env python3
"""Run the GameCube build in Dolphin, capture screenshots, and ask a vision model."""

from __future__ import annotations

import argparse
import base64
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen


def run(command: list[str], root: Path, *, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
	print("$ " + " ".join(command), flush=True)
	return subprocess.run(command, cwd=root, text=True, check=False,
		capture_output=True, env=env or os.environ.copy())


def write_config(user_dir: Path) -> None:
	config = user_dir / "Config"
	config.mkdir(parents=True, exist_ok=True)
	(config / "Dolphin.ini").write_text("""[Core]
CPUCore = 0
CPUThread = False
DSPHLE = True
FastDiscSpeed = True
[Interface]
ConfirmStop = False
[Display]
RenderToMain = True
""", encoding="utf-8")
	(config / "Logger.ini").write_text("""[Logs]
BOOT = True
CORE = True
DVD = False
OSREPORT = True
OSREPORT_HLE = True
PowerPC = False
[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = True
WriteToWindow = False
""", encoding="utf-8")


def repo_dolphin(root: Path) -> Path | None:
	candidates = (
		root / "3rdparty/dolphin/build/Binaries/dolphin-emu",
		root / "3rdparty/dolphin/build/Binaries/dolphin-emu-nogui",
		root / "3rdparty/dolphin/build/dolphin-emu",
		root / "3rdparty/dolphin/build/dolphin-emu-nogui",
	)
	for candidate in candidates:
		if candidate.is_file() and os.access(candidate, os.X_OK):
			return candidate
	return None


def dolphin_command(root: Path, user_dir: Path, iso: Path) -> tuple[list[str], bool]:
	if os.environ.get("DOLPHIN_EXECUTABLE", "").startswith("flatpak:"):
		flatpak_id = os.environ["DOLPHIN_EXECUTABLE"].removeprefix("flatpak:")
		return ["flatpak", "run", f"--filesystem={root}", flatpak_id,
			"-u", str(user_dir), "-l", "-b", "-e", str(iso)], True
	if os.environ.get("DOLPHIN_EXECUTABLE"):
		return [os.environ["DOLPHIN_EXECUTABLE"], "-u", str(user_dir), "-l", "-b", "-e", str(iso)], False
	local = repo_dolphin(root)
	if local:
		return [str(local), "-u", str(user_dir), "-l", "-b", "-e", str(iso)], False
	for name in ("dolphin-emu", "dolphin"):
		found = shutil.which(name)
		if found:
			return [found, "-u", str(user_dir), "-l", "-b", "-e", str(iso)], False
	if shutil.which("flatpak"):
		flatpak_id = os.environ.get("DOLPHIN_FLATPAK_ID", "org.DolphinEmu.dolphin-emu")
		return ["flatpak", "run", f"--filesystem={root}", flatpak_id,
			"-u", str(user_dir), "-l", "-b", "-e", str(iso)], True
	raise FileNotFoundError("Dolphin executable, submodule build, or Flatpak was not found")


def screenshot_command(output: Path) -> list[str] | None:
	candidates = (
		("gnome-screenshot", ["gnome-screenshot", "-f", str(output)]),
		("spectacle", ["spectacle", "-b", "-n", "-o", str(output)]),
		("scrot", ["scrot", str(output)]),
		("maim", ["maim", str(output)]),
		("grim", ["grim", str(output)]),
		("import", ["import", "-window", "root", str(output)]),
	)
	for binary, command in candidates:
		if shutil.which(binary):
			return command
	return None


def capture_screenshot(output: Path, root: Path) -> bool:
	command = screenshot_command(output)
	if command:
		result = run(command, root)
		if result.stdout:
			print(result.stdout, end="")
		if result.stderr:
			print(result.stderr, end="", file=sys.stderr)
		if result.returncode == 0 and output.is_file() and output.stat().st_size > 0:
			return True

	try:
		from PyQt6.QtGui import QGuiApplication
	except ImportError:
		return False

	app = QGuiApplication.instance() or QGuiApplication(["dolphin-vision-capture"])
	screen = QGuiApplication.primaryScreen()
	if screen is None:
		return False
	pixmap = screen.grabWindow(0)
	if pixmap.isNull():
		return False
	return pixmap.save(str(output), "PNG") and output.is_file() and output.stat().st_size > 0


def api_url(api_base: str) -> str:
	base = api_base.rstrip("/")
	if urlparse(base).path.rstrip("/").endswith("/v1"):
		return base + "/chat/completions"
	return base + "/v1/chat/completions"


def read_tail(path: Path, limit: int = 12000) -> str:
	if not path.is_file():
		return ""
	data = path.read_text(encoding="utf-8", errors="replace")
	return data[-limit:]


def analyze_with_vision(image: Path, logs: str, args: argparse.Namespace) -> str:
	image_b64 = base64.b64encode(image.read_bytes()).decode("ascii")
	prompt = (
		"You are reviewing an automated Dolphin screenshot from the Xash3D "
		"Half-Life GameCube port. Decide whether the latest code change appears "
		"to have worked visually. Look for black screen, visible diagnostic marker, "
		"world geometry, HUD text/icons, menus, crash dialogs, or emulator errors. "
		"Use the OSReport/Dolphin log excerpt as supporting evidence. Return a "
		"short verdict with: status=pass/fail/inconclusive, observed visuals, "
		"relevant log clues, and the next debugging action.\n\n"
		f"Log excerpt:\n{logs}"
	)
	body = {
		"model": args.vision_model,
		"messages": [{
			"role": "user",
			"content": [
				{"type": "text", "text": prompt},
				{"type": "image_url", "image_url": {
					"url": f"data:image/png;base64,{image_b64}"
				}},
			],
		}],
		"max_tokens": args.max_tokens,
	}
	request = Request(
		api_url(args.api_base),
		data=json.dumps(body).encode("utf-8"),
		headers={"Content-Type": "application/json"},
		method="POST",
	)
	if args.api_key:
		request.add_header("Authorization", f"Bearer {args.api_key}")
	with urlopen(request, timeout=args.vision_timeout) as response:
		payload = json.loads(response.read().decode("utf-8"))
	return payload["choices"][0]["message"]["content"]


def terminate(proc: subprocess.Popen[str], *, flatpak: bool) -> None:
	if proc.poll() is None:
		proc.terminate()
		try:
			proc.wait(timeout=5)
		except subprocess.TimeoutExpired:
			proc.kill()
			proc.wait(timeout=5)
	if flatpak and shutil.which("flatpak"):
		flatpak_id = os.environ.get("DOLPHIN_FLATPAK_ID", "org.DolphinEmu.dolphin-emu")
		subprocess.run(["flatpak", "kill", flatpak_id], text=True,
			capture_output=True, check=False)


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--repo", type=Path, default=Path.cwd())
	parser.add_argument("--runtime", type=int, default=int(os.environ.get("DOLPHIN_VISION_RUNTIME", "45")))
	parser.add_argument("--first-screenshot", type=int,
		default=int(os.environ.get("DOLPHIN_VISION_FIRST_SCREENSHOT", "12")))
	parser.add_argument("--screenshot-interval", type=int,
		default=int(os.environ.get("DOLPHIN_VISION_SCREENSHOT_INTERVAL", "10")))
	parser.add_argument("--smoke-map", default=os.environ.get("DOLPHIN_SMOKE_MAP", "c0a0e"))
	parser.add_argument("--api-base", default=os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1"))
	parser.add_argument("--api-key", default=os.environ.get("OPENAI_API_KEY", ""))
	parser.add_argument("--vision-model", default=os.environ.get(
		"QWABLE_5_VISION_MODEL", os.environ.get("QWABLE_VISION_MODEL", "qwable-5-vision")))
	parser.add_argument("--max-tokens", type=int, default=600)
	parser.add_argument("--vision-timeout", type=int, default=120)
	parser.add_argument("--skip-vision", action="store_true")
	args = parser.parse_args()

	root = args.repo.resolve()
	log_dir = root / ".ai/logs" / f"dolphin-vision-{datetime.now().strftime('%Y%m%d-%H%M%S')}"
	user_dir = log_dir / "dolphin-user"
	screens = log_dir / "screenshots"
	log_dir.mkdir(parents=True, exist_ok=True)
	screens.mkdir(parents=True, exist_ok=True)
	write_config(user_dir)

	iso = root / "OUT/xash3d-gc.iso"
	build = run(["scripts/build-gamecube-disc.py", "--output", str(iso),
		"--smoke-map", args.smoke_map], root)
	(log_dir / "disc-build.stdout.log").write_text(build.stdout, encoding="utf-8")
	(log_dir / "disc-build.stderr.log").write_text(build.stderr, encoding="utf-8")
	print(build.stdout, end="")
	print(build.stderr, end="", file=sys.stderr)
	if build.returncode != 0:
		print(f"FAIL: disc build failed. Logs: {log_dir}")
		return build.returncode

	try:
		command, flatpak = dolphin_command(root, user_dir, iso)
	except FileNotFoundError as exc:
		print(f"HOST_FAILURE: {exc}")
		return 2

	print("$ " + " ".join(command), flush=True)
	stdout = open(log_dir / "dolphin.stdout.log", "w", encoding="utf-8")
	stderr = open(log_dir / "dolphin.stderr.log", "w", encoding="utf-8")
	proc = subprocess.Popen(command, cwd=root, text=True, stdout=stdout, stderr=stderr,
		start_new_session=True)
	latest_screen: Path | None = None
	next_capture = time.monotonic() + args.first_screenshot
	deadline = time.monotonic() + args.runtime
	try:
		while time.monotonic() < deadline and proc.poll() is None:
			if time.monotonic() >= next_capture:
				output = screens / f"screen-{len(list(screens.glob('screen-*.png'))) + 1:02d}.png"
				if capture_screenshot(output, root):
					latest_screen = output
					print(f"SCREENSHOT: {output.relative_to(root)}")
				else:
					print(
						"SCREENSHOT_FAILURE: no screenshot captured; install gnome-screenshot, "
						"spectacle, scrot, maim, grim, or run from a desktop session where "
						"PyQt6 can capture the primary screen"
					)
				next_capture += args.screenshot_interval
			time.sleep(0.5)
	finally:
		terminate(proc, flatpak=flatpak)
		stdout.close()
		stderr.close()

	logs = "\n".join((
		read_tail(log_dir / "dolphin.stdout.log"),
		read_tail(log_dir / "dolphin.stderr.log"),
	))
	if latest_screen and not args.skip_vision:
		try:
			analysis = analyze_with_vision(latest_screen, logs, args)
		except (OSError, URLError, KeyError, json.JSONDecodeError) as exc:
			analysis = f"VISION_FAILURE: {exc}"
		(log_dir / "vision-analysis.md").write_text(analysis + "\n", encoding="utf-8")
		print("\n== vision analysis ==")
		print(analysis)
	elif not latest_screen:
		print("VISION_SKIPPED: no screenshot was captured.")
	else:
		print("VISION_SKIPPED: --skip-vision was set.")

	print(f"Logs: {log_dir.relative_to(root)}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
