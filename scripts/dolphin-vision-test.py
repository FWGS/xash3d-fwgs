#!/usr/bin/env python3
"""Run the GameCube build in Dolphin, capture screenshots, and ask a vision model."""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
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


def write_config(user_dir: Path, *, frame_dump_fallback: bool = False) -> None:
	config = user_dir / "Config"
	config.mkdir(parents=True, exist_ok=True)
	movie_settings = """[Movie]
DumpFrames = True
DumpFramesSilent = True
""" if frame_dump_fallback else ""
	(config / "Dolphin.ini").write_text(f"""[Core]
CPUCore = 0
CPUThread = False
DSPHLE = True
FastDiscSpeed = True
[Analytics]
Enabled = False
PermissionAsked = True
[Interface]
ConfirmStop = False
[Display]
RenderToMain = True
{movie_settings}""", encoding="utf-8")
	if frame_dump_fallback:
		(config / "GFX.ini").write_text("""[Settings]
DumpFramesAsImages = True
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


def dolphin_command(root: Path, user_dir: Path, iso: Path, *, batch: bool = True) -> tuple[list[str], bool]:
	mode_args = ["-l", "-b"] if batch else ["-l"]
	if os.environ.get("DOLPHIN_EXECUTABLE", "").startswith("flatpak:"):
		flatpak_id = os.environ["DOLPHIN_EXECUTABLE"].removeprefix("flatpak:")
		return ["flatpak", "run", f"--filesystem={root}", flatpak_id,
			"-u", str(user_dir), *mode_args, "-e", str(iso)], True
	if os.environ.get("DOLPHIN_EXECUTABLE"):
		return [os.environ["DOLPHIN_EXECUTABLE"], "-u", str(user_dir), *mode_args, "-e", str(iso)], False
	local = repo_dolphin(root)
	if local:
		return [str(local), "-u", str(user_dir), *mode_args, "-e", str(iso)], False
	for name in ("dolphin-emu", "dolphin"):
		found = shutil.which(name)
		if found:
			return [found, "-u", str(user_dir), *mode_args, "-e", str(iso)], False
	if shutil.which("flatpak"):
		flatpak_id = os.environ.get("DOLPHIN_FLATPAK_ID", "org.DolphinEmu.dolphin-emu")
		return ["flatpak", "run", f"--filesystem={root}", flatpak_id,
			"-u", str(user_dir), *mode_args, "-e", str(iso)], True
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


def latest_frame_dump(user_dir: Path) -> Path | None:
	frame_root = user_dir / "Dump/Frames"
	if not frame_root.is_dir():
		return None
	candidates = sorted(frame_root.rglob("*.png"),
		key=lambda path: path.stat().st_mtime if path.exists() else 0)
	return candidates[-1] if candidates else None


def capture_screenshot(output: Path, root: Path, *, user_dir: Path | None = None) -> bool:
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
		if user_dir:
			frame = latest_frame_dump(user_dir)
			if frame:
				shutil.copy2(frame, output)
				return output.is_file() and output.stat().st_size > 0
		return False

	app = QGuiApplication.instance() or QGuiApplication(["dolphin-vision-capture"])
	screen = QGuiApplication.primaryScreen()
	if screen is None:
		if user_dir:
			frame = latest_frame_dump(user_dir)
			if frame:
				shutil.copy2(frame, output)
				return output.is_file() and output.stat().st_size > 0
		return False
	pixmap = screen.grabWindow(0)
	if pixmap.isNull():
		if user_dir:
			frame = latest_frame_dump(user_dir)
			if frame:
				shutil.copy2(frame, output)
				return output.is_file() and output.stat().st_size > 0
		return False
	if pixmap.save(str(output), "PNG") and output.is_file() and output.stat().st_size > 0:
		return True
	if user_dir:
		frame = latest_frame_dump(user_dir)
		if frame:
			shutil.copy2(frame, output)
			return output.is_file() and output.stat().st_size > 0
	return False


def parse_state_captures(raw: str) -> list[tuple[str, int]]:
	states: list[tuple[str, int]] = []
	for item in raw.split(","):
		item = item.strip()
		if not item:
			continue
		name, sep, seconds = item.partition(":")
		if not sep:
			continue
		try:
			capture_at = int(seconds)
		except ValueError:
			continue
		safe_name = re.sub(r"[^A-Za-z0-9_-]+", "-", name.strip().lower()).strip("-")
		if safe_name:
			states.append((safe_name, max(0, capture_at)))
	return sorted(states, key=lambda pair: pair[1])


def api_url(api_base: str) -> str:
	base = api_base.rstrip("/")
	if urlparse(base).path.rstrip("/").endswith("/v1"):
		return base + "/chat/completions"
	return base + "/v1/chat/completions"


def read_tail(path: Path, limit: int = 64000) -> str:
	if not path.is_file():
		return ""
	data = path.read_text(encoding="utf-8", errors="replace")
	return data[-limit:]


GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |~|x|X|MANUAL)\]\s+(.+)$")


def active_goal(root: Path) -> str:
	goals = root / ".ai/goals/GAMECUBE_PORT_GOALS.md"
	if not goals.is_file():
		return "dolphin"
	current: tuple[str, str, str] | None = None
	body: list[str] = []
	for line in goals.read_text(encoding="utf-8").splitlines():
		match = GOAL_RE.match(line)
		if match:
			if current and current[1] in {" ", "~"} and not goal_blocked(body):
				return current[0]
			current = match.groups()
			body = []
		elif current:
			body.append(line)
	if current and current[1] in {" ", "~"} and not goal_blocked(body):
		return current[0]
	return "dolphin"


def goal_blocked(body: list[str]) -> bool:
	return any(re.match(r"\s*-\s*Status:\s*BLOCKED\b", line, re.IGNORECASE)
		for line in body)


def marker_bool(logs: str, *patterns: str) -> bool:
	return any(re.search(pattern, logs, re.IGNORECASE | re.MULTILINE)
		for pattern in patterns)


def image_nonblack_metrics(image: Path | None) -> dict[str, object]:
	if image is None:
		return {"available": False, "nonblack_pixels": 0, "sampled_nonblack": False}
	try:
		from PIL import Image
	except ImportError:
		return {"available": True, "nonblack_pixels": 0, "sampled_nonblack": False}
	try:
		with Image.open(image) as handle:
			rgb = handle.convert("RGB")
			pixels = rgb.get_flattened_data() if hasattr(rgb, "get_flattened_data") else rgb.getdata()
			pixel_list = list(pixels)
			total = len(pixel_list)
			nonblack = sum(1 for pixel in pixel_list if pixel != (0, 0, 0))
			colors = rgb.getcolors(maxcolors=4096)
	except OSError:
		return {"available": True, "nonblack_pixels": 0, "sampled_nonblack": False}
	dominant_color = None
	dominant_ratio = 0.0
	flat_artifact = False
	if colors and total:
		dominant_count, dominant_color = max(colors, key=lambda item: item[0])
		dominant_ratio = dominant_count / total
		flat_artifact = dominant_color != (0, 0, 0) and dominant_ratio >= 0.98
	return {
		"available": True,
		"nonblack_pixels": nonblack,
		"sampled_nonblack": nonblack > 0 and not flat_artifact,
		"dominant_color": dominant_color,
		"dominant_ratio": dominant_ratio,
		"flat_artifact": flat_artifact,
	}


def classify_logs(logs: str, smoke_map: str,
	image_metrics: dict[str, object] | None = None) -> dict[str, object]:
	image_metrics = image_metrics or {}
	markers = {
		"bootstrap": marker_bool(logs, r"Xash3D GameCube: bootstrap"),
		"engine_ready": marker_bool(logs, r"Xash3D GameCube: engine subsystems ready"),
		"map_loaded": marker_bool(logs, rf"Xash3D GameCube: map loaded {re.escape(smoke_map)}"),
		"input_polling": marker_bool(logs, r"Xash3D GameCube: input polling active"),
		"resource_verification": marker_bool(logs,
			r"Verifying and downloading resources", r"ucmd->sendres\(\)"),
		"diagnostic_marker": marker_bool(logs, r"DIAGNOSTIC MARKER VISIBLE"),
		"sampled_nonblack": marker_bool(logs, r"sampled_nonblack=1"),
		"audio_voice_started": marker_bool(logs, r"audio voice started"),
		"audio_nonzero_pcm": marker_bool(logs, r"audio submitted nonzero PCM"),
		"intro_requested": marker_bool(logs, r"Xash3D GameCube: intro AVI play"),
		"intro_opened": marker_bool(logs, r"Xash3D GameCube: intro AVI opened"),
		"intro_first_frame": marker_bool(logs, r"Xash3D GameCube: intro AVI decoded first frame"),
	}
	if image_metrics.get("sampled_nonblack"):
		markers["sampled_nonblack"] = True
	errors = sorted(set(re.findall(
		r"(Host_ErrorInit:.*|Host_Error:.*|Sys_Error:.*|fatal error.*|out of memory.*|Unknown instruction.*|Invalid read from.*)",
		logs, re.IGNORECASE)))
	if errors:
		status = "guest_failure" if markers["bootstrap"] else "host_or_boot_failure"
	elif markers["intro_first_frame"] and markers["sampled_nonblack"]:
		status = "intro_avi_nonblack"
	elif markers["intro_first_frame"]:
		status = "intro_avi_decoded"
	elif markers["intro_opened"]:
		status = "intro_avi_opened"
	elif markers["intro_requested"]:
		status = "intro_avi_requested"
	elif markers["map_loaded"] and markers["sampled_nonblack"]:
		status = "active_rendering_nonblack"
	elif markers["map_loaded"] and markers["input_polling"]:
		status = "map_ready"
	elif markers["map_loaded"] and markers["resource_verification"]:
		status = "map_loaded_waiting_for_client_resources"
	elif markers["map_loaded"]:
		status = "map_loaded"
	elif markers["engine_ready"]:
		status = "engine_ready"
	elif markers["bootstrap"]:
		status = "bootstrap_only"
	else:
		status = "inconclusive"
	visual = "unknown"
	if markers["intro_first_frame"] and markers["sampled_nonblack"]:
		visual = "intro_avi_nonblack_frame"
	elif markers["sampled_nonblack"]:
		visual = "nonblack_frame_dump" if image_metrics.get("sampled_nonblack") else "nonblack_renderer_pixels"
	elif markers["intro_first_frame"]:
		visual = "intro_avi_decoded_no_nonblack_capture"
	elif markers["diagnostic_marker"]:
		visual = "diagnostic_marker_only"
	elif markers["map_loaded"]:
		visual = "no_nonblack_log_marker"
	audio = "unknown"
	if markers["audio_nonzero_pcm"]:
		audio = "nonzero_pcm_submitted"
	elif markers["audio_voice_started"]:
		audio = "voice_started_no_nonzero_pcm"
	return {
		"status": status,
		"visual": visual,
		"audio": audio,
		"markers": markers,
		"errors": errors[:10],
		"image": image_metrics,
	}


def next_action(classification: dict[str, object], screenshot_available: bool,
	vision_available: bool) -> str:
	markers = classification.get("markers", {})
	errors = classification.get("errors", [])
	if errors:
		return "Fix the first guest error in OSReport before visual/audio tuning."
	if markers.get("intro_first_frame") and markers.get("sampled_nonblack"):
		return "Intro AVI decoded and produced nonblack output; preserve native AVI playback and move to menu/gameplay proof."
	if markers.get("intro_first_frame"):
		return "Native AVI decode reached first frame; improve screenshot/frame capture or renderer presentation proof."
	if markers.get("intro_opened"):
		return "AVI opened but no decoded-frame marker appeared; debug Cinepak frame decode or timing."
	if markers.get("intro_requested"):
		return "Startup video command ran but AVI did not open; inspect filesystem path, playlist, and staged media."
	if markers.get("map_loaded") and markers.get("sampled_nonblack"):
		return "Nonblack active-render evidence captured; preserve this route and work the next gameplay/audio fidelity gate."
	if markers.get("map_loaded") and markers.get("resource_verification") and not markers.get("sampled_nonblack"):
		return "Advance client resource verification/prespawn to active rendering and nonblack frame output."
	if markers.get("map_loaded") and markers.get("input_polling"):
		return "Map and input are alive; focus on prespawn/resource completion and renderer nonblack output."
	if not markers.get("bootstrap"):
		return "Debug Dolphin launch, disc boot, apploader, or executable selection."
	if not markers.get("engine_ready"):
		return "Use OSReport tail to find the subsystem before engine readiness."
	if not markers.get("map_loaded"):
		return "Focus on map load, filesystem, model, or spawn blockers."
	if not markers.get("input_polling"):
		return "Check GameCube PAD/input initialization and polling markers."
	if classification.get("visual") == "diagnostic_marker_only":
		return "VI/XFB is likely alive; debug renderer content path and HUD/world draws."
	if not screenshot_available:
		return "Install a screenshot tool or run the GUI from a capturable desktop session."
	if not vision_available:
		return "Start a Qwable vision endpoint or set QWABLE_5_VISION_MODEL, then rerun."
	return "Use the vision verdict plus markers to choose the next renderer/HUD/audio slice."


def analyze_text_only(logs: str, classification: dict[str, object],
	args: argparse.Namespace) -> str:
	body = {
		"model": args.vision_model,
		"messages": [{
			"role": "user",
			"content": (
				"You are reviewing a Dolphin run of the Xash3D Half-Life GameCube port. "
				"No screenshot was captured, so use only structured markers and log tail. "
				"Return status=pass/fail/inconclusive, what worked, what is blocked, and "
				"the next concrete debugging action.\n\n"
				f"Markers:\n{json.dumps(classification, indent=2)}\n\nLog excerpt:\n{logs}"
			),
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


def write_memory(root: Path, run: dict[str, object]) -> None:
	state_dir = root / ".ai/state"
	state_dir.mkdir(parents=True, exist_ok=True)
	path = state_dir / "dolphin-harness-memory.json"
	if path.is_file():
		try:
			memory = json.loads(path.read_text(encoding="utf-8"))
		except json.JSONDecodeError:
			memory = {}
	else:
		memory = {}
	memory.setdefault("purpose", "Persistent Dolphin run memory for Qwable/Codex porting feedback.")
	memory["updated_at"] = datetime.now().astimezone().isoformat(timespec="seconds")
	runs = memory.setdefault("runs", [])
	runs.insert(0, run)
	del runs[20:]
	goal = str(run.get("goal", "dolphin"))
	rooms = memory.setdefault("rooms", {})
	room = rooms.setdefault(goal, {"recent": []})
	room["last"] = run
	room["recent"].insert(0, run)
	del room["recent"][8:]
	path.write_text(json.dumps(memory, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	latest = state_dir / "dolphin-harness-latest.md"
	latest.write_text(
		"\n".join((
			"# Dolphin Harness Latest",
			"",
			f"- Goal: {run.get('goal')}",
			f"- Status: {run.get('classification', {}).get('status')}",
			f"- Visual: {run.get('classification', {}).get('visual')}",
			f"- Audio: {run.get('classification', {}).get('audio')}",
			f"- Screenshot: {run.get('latest_screenshot') or 'none'}",
			f"- Analysis: {run.get('analysis_path') or 'none'}",
			f"- Logs: {run.get('log_dir')}",
			f"- Next action: {run.get('next_action')}",
			"",
			"## Model Analysis",
			str(run.get("analysis", "(none)")),
			"",
		)),
		encoding="utf-8",
	)


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
	parser.add_argument("--state-captures", default=os.environ.get(
		"DOLPHIN_STATE_CAPTURES", "intro-avi:8,main-menu:18,gameplay:35"),
		help="comma-separated label:seconds screenshots to capture for GUI validation")
	parser.add_argument("--smoke-map", default=os.environ.get("DOLPHIN_SMOKE_MAP", "c0a0e"))
	parser.add_argument("--boot-mode", choices=("smoke", "intro-avi"), default=os.environ.get(
		"DOLPHIN_VISION_BOOT_MODE", "smoke"),
		help="smoke loads a map with -nointro; intro-avi stages original local AVI files and tests startup video")
	parser.add_argument("--api-base", default=os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1"))
	parser.add_argument("--api-key", default=os.environ.get("OPENAI_API_KEY", ""))
	parser.add_argument("--vision-model", default=os.environ.get(
		"QWABLE_5_VISION_MODEL", os.environ.get("QWABLE_VISION_MODEL", "qwable-5-vision")))
	parser.add_argument("--max-tokens", type=int, default=600)
	parser.add_argument("--vision-timeout", type=int, default=120)
	parser.add_argument("--goal", default=None,
		help="goal/memory room for this run; defaults to the active automatic goal")
	parser.add_argument("--skip-vision", action="store_true")
	parser.add_argument("--skip-text-analysis", action="store_true",
		help="do not ask the model for log-only analysis when no screenshot is available")
	parser.add_argument("--no-frame-dump-fallback", action="store_true",
		help="do not enable Dolphin PNG frame dumping when desktop screenshot tools are unavailable")
	parser.add_argument("--no-batch", action="store_true",
		help="launch Dolphin without -b so GUI/manual-like boots do not exit as soon as batch mode considers emulation complete")
	parser.add_argument("--no-memory", action="store_true",
		help="do not update .ai/state/dolphin-harness-memory.json")
	args = parser.parse_args()

	root = args.repo.resolve()
	goal = args.goal or active_goal(root)
	log_dir = root / ".ai/logs" / f"dolphin-vision-{datetime.now().strftime('%Y%m%d-%H%M%S')}"
	user_dir = log_dir / "dolphin-user"
	screens = log_dir / "screenshots"
	log_dir.mkdir(parents=True, exist_ok=True)
	screens.mkdir(parents=True, exist_ok=True)
	frame_dump_fallback = not args.no_frame_dump_fallback and screenshot_command(screens / "probe.png") is None
	write_config(user_dir, frame_dump_fallback=frame_dump_fallback)

	iso = root / "OUT/xash3d-gc.iso"
	build_args = ["scripts/build-gamecube-disc.py", "--output", str(iso)]
	if args.boot_mode == "smoke":
		build_args.extend(("--smoke-map", args.smoke_map))
	else:
		build_args.append("--intro-avi")
	build = run(build_args, root)
	(log_dir / "disc-build.stdout.log").write_text(build.stdout, encoding="utf-8")
	(log_dir / "disc-build.stderr.log").write_text(build.stderr, encoding="utf-8")
	print(build.stdout, end="")
	print(build.stderr, end="", file=sys.stderr)
	if build.returncode != 0:
		print(f"FAIL: disc build failed. Logs: {log_dir}")
		return build.returncode

	try:
		command, flatpak = dolphin_command(root, user_dir, iso, batch=not args.no_batch)
	except FileNotFoundError as exc:
		print(f"HOST_FAILURE: {exc}")
		return 2

	print("$ " + " ".join(command), flush=True)
	stdout = open(log_dir / "dolphin.stdout.log", "w", encoding="utf-8")
	stderr = open(log_dir / "dolphin.stderr.log", "w", encoding="utf-8")
	proc = subprocess.Popen(command, cwd=root, text=True, stdout=stdout, stderr=stderr,
		start_new_session=True)
	latest_screen: Path | None = None
	state_screenshots: dict[str, str] = {}
	pending_states = parse_state_captures(args.state_captures)
	started_at = time.monotonic()
	next_capture = time.monotonic() + args.first_screenshot
	deadline = time.monotonic() + args.runtime
	try:
		while time.monotonic() < deadline and proc.poll() is None:
			elapsed = int(time.monotonic() - started_at)
			while pending_states and elapsed >= pending_states[0][1]:
				state_name, _capture_at = pending_states.pop(0)
				output = screens / f"state-{state_name}.png"
				if capture_screenshot(output, root, user_dir=user_dir):
					latest_screen = output
					state_screenshots[state_name] = str(output.relative_to(root))
					print(f"STATE_SCREENSHOT: {state_name} {output.relative_to(root)}")
				else:
					print(f"STATE_SCREENSHOT_FAILURE: {state_name}")
			if time.monotonic() >= next_capture:
				output = screens / f"screen-{len(list(screens.glob('screen-*.png'))) + 1:02d}.png"
				if capture_screenshot(output, root, user_dir=user_dir):
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
		dolphin_returncode = proc.returncode
		stdout.close()
		stderr.close()
	if latest_screen is None:
		frame = latest_frame_dump(user_dir)
		if frame:
			output = screens / "screen-framedump-latest.png"
			shutil.copy2(frame, output)
			if output.is_file() and output.stat().st_size > 0:
				latest_screen = output
				print(f"SCREENSHOT: {output.relative_to(root)}")

	logs = "\n".join((
		read_tail(log_dir / "dolphin.stdout.log"),
		read_tail(log_dir / "dolphin.stderr.log"),
	))
	image_metrics = image_nonblack_metrics(latest_screen)
	classification = classify_logs(logs, args.smoke_map, image_metrics)
	analysis = ""
	analysis_path = ""
	if latest_screen and not args.skip_vision:
		try:
			analysis = analyze_with_vision(latest_screen, logs, args)
		except (OSError, URLError, KeyError, json.JSONDecodeError) as exc:
			analysis = f"VISION_FAILURE: {exc}"
		analysis_path = str((log_dir / "vision-analysis.md").relative_to(root))
		(log_dir / "vision-analysis.md").write_text(analysis + "\n", encoding="utf-8")
		print("\n== vision analysis ==")
		print(analysis)
	elif not latest_screen:
		print("VISION_SKIPPED: no screenshot was captured.")
		if not args.skip_text_analysis and not args.skip_vision:
			try:
				analysis = analyze_text_only(logs, classification, args)
			except (OSError, URLError, KeyError, json.JSONDecodeError) as exc:
				analysis = f"TEXT_ANALYSIS_FAILURE: {exc}"
			analysis_path = str((log_dir / "log-analysis.md").relative_to(root))
			(log_dir / "log-analysis.md").write_text(analysis + "\n", encoding="utf-8")
			print("\n== log analysis ==")
			print(analysis)
	else:
		print("VISION_SKIPPED: --skip-vision was set.")

	result = {
		"goal": goal,
		"created_at": datetime.now().astimezone().isoformat(timespec="seconds"),
		"command": command,
		"flatpak": flatpak,
		"batch": not args.no_batch,
		"dolphin_returncode": dolphin_returncode,
		"smoke_map": args.smoke_map,
		"boot_mode": args.boot_mode,
		"log_dir": str(log_dir.relative_to(root)),
		"latest_screenshot": str(latest_screen.relative_to(root)) if latest_screen else "",
		"state_screenshots": state_screenshots,
		"frame_dump_fallback": frame_dump_fallback,
		"analysis_path": analysis_path,
		"analysis": analysis,
		"classification": classification,
		"next_action": next_action(classification, latest_screen is not None, bool(analysis_path)),
	}
	(log_dir / "result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
		encoding="utf-8")
	if not args.no_memory:
		write_memory(root, result)
	print("\n== harness classification ==")
	print(json.dumps({
		"goal": goal,
		"classification": classification,
		"next_action": result["next_action"],
		"result": str((log_dir / "result.json").relative_to(root)),
	}, indent=2))
	print(f"Logs: {log_dir.relative_to(root)}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
