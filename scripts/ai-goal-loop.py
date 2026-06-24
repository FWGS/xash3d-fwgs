#!/usr/bin/env python3
"""Advance the Xash3D GameCube port through evidence-gated Aider goals."""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import shutil
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |~|x|X|MANUAL)\]\s+(.+)$")
DOLPHIN_PROBE_GOALS = frozenset({"G14", "G19", "G21", "G34", "G35", "G40"})
PROBE_AUTO_COMPLETE = {
	"G19": "MAP_READY:",
}
MEMORY_FILE = Path(".ai/state/goal-loop-memory.json")
MEMORY_MAX_DRAWERS = 12
MEMORY_MAX_TOOL_CALLS = 40
FAULT_PATTERNS = (
	r"Host_ErrorInit:.*",
	r"Host_Error:.*",
	r"Sys_Error:.*",
	r"_Mem_Alloc.*",
	r"Could not load .*",
	r"missing .*",
	r"fatal.*",
	r"verify: .*failed",
	r"token/context limit",
	r"Aider made no edit",
	r"MAP_READY:.*",
	r"DIAGNOSTIC MARKER VISIBLE",
	r"black screen",
	r"audio .*",
	r"read-only fallback.*",
)
COMMON_CONTEXT = (
	"docs/GAMECUBE_PORT_PLAN.md",
	".ai/goals/GAMECUBE_PORT_GOALS.md",
)
COMMON_READ_CONTEXT = (
	".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
	".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
)
GOAL_CONTEXT = {
	"G01": ("engine/server/sv_game.c", "engine/server/server.h",
		"engine/edict.h", "engine/progdefs.h"),
	"G02": ("scripts/build-gamecube-disc.py", "scripts/gamecube-apploader.c",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G03": ("engine/platform/gamecube/vid_gamecube.c", "ref/gx/r_context.c",
		"ref/gx/r_main.c", "ref/gx/r_local.h"),
	"G04": ("engine/platform/gamecube/in_gamecube.c", "engine/client/input.h",
		"engine/client/input/input.c"),
	"G05": ("engine/client/sound/s_main.c", "engine/client/sound.h",
		"engine/platform/gamecube/dll_gamecube.c"),
	"G06": ("engine/platform/gamecube/sys_gamecube.c", "engine/host.c",
		"filesystem/filesystem.c"),
	"G07": (),
	"G09": ("scripts/hlsdk-gamecube-probe.sh", "scripts/build-gamecube.sh",
		"scripts/ai-verify.sh", "Documentation/development/engine-porting-guide.md"),
	"G10": ("scripts/hlsdk-gamecube-probe.sh", "scripts/hlsdk-gamecube-build.sh",
		"scripts/build-gamecube.sh",
		"scripts/gha/build_nswitch_docker.sh", "scripts/gha/build_psvita.sh"),
	"G11": ("scripts/hlsdk-gamecube-probe.sh", "scripts/hlsdk-gamecube-build.sh",
		"scripts/hlsdk-gamecube-apply-patch.py",
		"Documentation/development/engine-porting-guide.md"),
	"G12": ("engine/platform/gamecube/dll_gamecube.c", "engine/wscript",
		"stub/client/client_export.c", "stub/server/server_export.c",
		"scripts/hlsdk-gamecube-build.sh", "scripts/hlsdk-gamecube-apply-patch.py"),
	"G13": ("engine/wscript", "stub/client/client_export.c",
		"scripts/hlsdk-gamecube-build.sh", "scripts/hlsdk-gamecube-apply-patch.py"),
	"G14": ("scripts/dolphin-boot-probe.sh", "scripts/build-gamecube-disc.py",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G16": ("engine/platform/gamecube/sys_gamecube.c",
		"engine/platform/gamecube/snddma_gamecube.c",
		"engine/client/cl_scrn.c",
		"engine/client/dll_int/cl_game.c"),
	"G17": ("engine/platform/gamecube/snddma_gamecube.c",
		"engine/client/sound/s_main.c", "engine/client/sound.h",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G18": ("engine/common/host.c", "engine/common/system.c",
		"engine/common/filesystem_engine.c", "engine/common/net_ws.h",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G19": ("scripts/dolphin-boot-probe.sh", "scripts/build-gamecube-disc.py",
		"engine/platform/gamecube/sys_gamecube.c",
		"engine/platform/gamecube/in_gamecube.c"),
	"G21": ("engine/common/host.c", "engine/common/model.c",
		"engine/common/filesystem_engine.c", "engine/common/system.c",
		"scripts/dolphin-boot-probe.sh"),
	"G22": ("engine/common/zone.c", "engine/common/common.h",
		"engine/common/host.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G23": ("engine/common/zone.c", "engine/common/mod_bmodel.c",
		"engine/common/mod_studio.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G24": ("engine/client/cl_scrn.c", "engine/client/cl_sprite.c",
		"engine/client/dll_int/cl_render.c", "engine/common/mod_studio.c",
		"engine/common/mod_bmodel.c"),
	"G25": ("engine/client/dll_int/cl_game.c", "engine/client/cl_scrn.c",
		"stub/client/client_export.c", "3rdparty/hlsdk-portable/cl_dll/hud.cpp",
		"3rdparty/hlsdk-portable/cl_dll/hud.h"),
	"G26": ("engine/platform/gamecube/snddma_gamecube.c",
		"engine/client/sound/s_main.c", "engine/client/soundlib/snd_main.c",
		"engine/client/sound.h"),
	"G27": ("engine/client/dll_int/cl_game.c",
		"engine/client/sound/s_main.c", "engine/common/sounds.c",
		"engine/platform/gamecube/snddma_gamecube.c"),
	"G28": ("engine/common/filesystem_engine.c", "engine/common/host.c",
		"engine/common/system.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G29": ("engine/common/net_ws.h", "engine/common/net_buffer.c",
		"engine/server/sv_client.c", "engine/client/cl_main.c",
		"engine/platform/gamecube/sys_gamecube.c"),
	"G30": ("engine/platform/gamecube/in_gamecube.c", "engine/client/input.h",
		"engine/client/input/input.c", "engine/client/console.c"),
	"G31": ("engine/server/sv_init.c", "engine/server/sv_client.c",
		"engine/client/cl_main.c", "engine/common/host.c"),
	"G32": ("engine/server/sv_init.c", "engine/server/sv_client.c",
		"engine/common/filesystem_engine.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G33": ("scripts/build-gamecube-disc.py", "scripts/dolphin-boot-probe.sh",
		"scripts/ai-verify.sh", ".gitignore"),
	"G34": ("scripts/dolphin-boot-probe.sh", "scripts/build-gamecube-disc.py",
		"engine/common/host.c", "engine/common/model.c"),
	"G35": ("scripts/dolphin-boot-probe.sh", "engine/server/sv_init.c",
		"engine/client/cl_main.c", "engine/platform/gamecube/in_gamecube.c"),
	"G36": ("engine/platform/gamecube/vid_gamecube.c", "engine/client/cl_scrn.c",
		"engine/common/mod_bmodel.c", "engine/common/mod_studio.c"),
	"G37": ("engine/common/host.c", "engine/common/system.c",
		"engine/common/zone.c", "engine/platform/gamecube/sys_gamecube.c",
		"scripts/dolphin-boot-probe.sh"),
	"G38": ("scripts/build-gamecube-disc.py", "scripts/dolphin-boot-probe.sh",
		"docs/GAMECUBE_PORT_PLAN.md"),
	"G39": ("scripts/build-gamecube-disc.py", "Documentation/development/engine-porting-guide.md",
		"docs/GAMECUBE_PORT_PLAN.md"),
	"G40": ("scripts/dolphin-boot-probe.sh", "scripts/build-gamecube-disc.py",
		"docs/GAMECUBE_PORT_PLAN.md"),
	"G41": ("scripts/build-gamecube.sh", "scripts/build-gamecube-disc.py",
		"scripts/dolphin-boot-probe.sh", "scripts/ai-verify.sh"),
	"G42": ("docs/GAMECUBE_PORT_PLAN.md", ".ai/goals/GAMECUBE_PORT_GOALS.md",
		"Documentation/development/engine-porting-guide.md"),
	"G43": ("scripts/build-gamecube.sh", "scripts/build-gamecube-disc.py",
		"scripts/dolphin-boot-probe.sh", "docs/GAMECUBE_HARDWARE_VALIDATION.md"),
	"G44": ("engine/platform/gamecube/vid_gamecube.c", "engine/client/cl_scrn.c",
		"docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G45": ("engine/platform/gamecube/in_gamecube.c", "engine/client/input.h",
		"engine/client/input/input.c", "docs/GAMECUBE_HARDWARE_VALIDATION.md"),
	"G46": ("engine/common/filesystem_engine.c", "engine/common/host.c",
		"engine/server/sv_init.c", "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G47": ("scripts/build-gamecube-disc.py", "engine/common/filesystem_engine.c",
		"scripts/ai-verify.sh", ".gitignore"),
	"G48": ("engine/platform/gamecube/snddma_gamecube.c",
		"engine/client/sound/s_main.c", "engine/client/soundlib/snd_main.c",
		".ai/prompts/GAMECUBE_AUDIO_NOTES.md"),
	"G49": ("engine/platform/gamecube/vid_gamecube.c", "engine/common/host.c",
		"engine/common/zone.c", ".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G50": ("engine/common/host.c", "engine/common/system.c",
		"engine/common/zone.c", "engine/platform/gamecube/sys_gamecube.c"),
	"G51": ("engine/platform/gamecube/in_gamecube.c", "engine/client/console.c",
		"engine/client/cl_scrn.c", "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G52": ("scripts/build-gamecube.sh", "scripts/build-gamecube-disc.py",
		"scripts/gamecube-homebrew-compliance-check.py",
		"docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G53": ("docs/GAMECUBE_HARDWARE_VALIDATION.md",
		"docs/GAMECUBE_PORT_PLAN.md", ".ai/goals/GAMECUBE_PORT_GOALS.md"),
	"G54": ("engine/common/host.c", "engine/common/zone.c",
		"scripts/dolphin-boot-probe.sh", "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
}
GOAL_READ_CONTEXT = {
	"G03": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G05": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G14": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G15": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G16": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md"),
	"G17": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",),
	"G18": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md"),
	"G19": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G21": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G22": (".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",),
	"G23": (".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",),
	"G24": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G25": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G26": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G27": (".ai/prompts/GAMECUBE_AUDIO_NOTES.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G28": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",),
	"G29": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",),
	"G30": (".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",),
	"G31": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G32": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G33": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",),
	"G34": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G35": (".ai/prompts/GAMECUBE_NETWORKING_NOTES.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G36": (".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G37": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G38": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G39": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",),
	"G40": (".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
		".ai/prompts/GAMECUBE_AUDIO_NOTES.md"),
	"G41": (".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G42": (".ai/prompts/GAMECUBE_CONTEXT_INDEX.md",
		".ai/prompts/GAMECUBE_HARDWARE_NOTES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G43": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md"),
	"G44": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md"),
	"G45": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md"),
	"G46": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md"),
	"G47": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_STORAGE_NOTES.md"),
	"G48": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_AUDIO_NOTES.md"),
	"G49": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G50": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
	"G51": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md"),
	"G52": (".ai/prompts/GAMECUBE_CONTEXT_INDEX.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G53": (".ai/prompts/GAMECUBE_HARDWARE_NOTES.md",
		".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
	"G54": (".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
		".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
}
GOAL_COMMIT_SUBJECT = {
	"G01": "fix: resolve GameCube edict warning audit",
	"G02": "feat: improve bounded Dolphin boot probing",
	"G03": "feat: advance GameCube GX video",
	"G04": "feat: advance GameCube controller input",
	"G05": "feat: advance GameCube audio",
	"G06": "feat: advance GameCube engine startup",
	"G07": "feat: advance GameCube map loading",
	"G09": "feat: probe GameCube HLSDK integration",
	"G10": "feat: build GameCube HLSDK",
	"G11": "feat: add GameCube HLSDK hooks",
	"G12": "feat: integrate GameCube HLSDK exports",
	"G13": "feat: integrate GameCube HLSDK client",
	"G14": "test: smoke GameCube map loading",
	"G16": "feat: stabilize GameCube client modes",
	"G17": "feat: advance GameCube audio mode",
	"G18": "feat: harden GameCube local startup",
	"G19": "test: prove GameCube gameplay smoke",
	"G21": "fix: resolve GameCube map lookup",
	"G22": "feat: add GameCube memory telemetry",
	"G23": "feat: define GameCube memory budget",
	"G24": "feat: stabilize GameCube visuals",
	"G25": "feat: stabilize GameCube HUD",
	"G26": "feat: add GameCube audio backend",
	"G27": "feat: define GameCube music policy",
	"G28": "feat: route GameCube writable storage",
	"G29": "feat: restore GameCube local networking",
	"G30": "feat: improve GameCube controls",
	"G31": "feat: support GameCube changelevel",
	"G32": "feat: support GameCube save load",
	"G33": "feat: validate GameCube content staging",
	"G34": "test: probe GameCube campaign maps",
	"G35": "test: prove GameCube early route",
	"G36": "perf: improve GameCube frame budget",
	"G37": "feat: harden GameCube diagnostics",
	"G38": "test: validate GameCube hardware",
	"G39": "docs: define GameCube loader matrix",
	"G40": "test: audit GameCube campaign",
	"G41": "build: prepare GameCube release scripts",
	"G42": "docs: finalize GameCube port guide",
	"G43": "test: validate GameCube boot media",
	"G44": "test: validate GameCube video modes",
	"G45": "feat: harden GameCube controller states",
	"G46": "feat: harden GameCube save integrity",
	"G47": "test: audit GameCube filesystem behavior",
	"G48": "test: validate GameCube audio behavior",
	"G49": "perf: validate GameCube frame timing",
	"G50": "feat: improve GameCube fatal errors",
	"G51": "feat: complete GameCube UX checks",
	"G52": "build: add GameCube release manifest",
	"G53": "docs: track GameCube hardware matrix",
	"G54": "test: add GameCube compliance evidence",
}
RECOVERABLE_EXIT_CODES = {
	10: "Aider made no edit",
	17: "Aider model call timed out",
	18: "Aider hit a token/context limit",
}


def load_dotenv(path: Path) -> None:
	"""Load simple KEY=VALUE entries without overriding the parent shell."""
	if not path.is_file():
		return
	for raw_line in path.read_text(encoding="utf-8").splitlines():
		line = raw_line.strip()
		if not line or line.startswith("#") or "=" not in line:
			continue
		if line.startswith("export "):
			line = line[len("export "):].lstrip()
		key, value = line.split("=", 1)
		key = key.strip()
		value = value.strip()
		if not key or key in os.environ:
			continue
		if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
			value = value[1:-1]
		os.environ[key] = value


@dataclass
class Goal:
	goal_id: str
	state: str
	title: str
	body: str

	@property
	def complete(self) -> bool:
		return self.state.lower() == "x"

	@property
	def manual(self) -> bool:
		return self.state == "MANUAL"

	@property
	def blocked(self) -> bool:
		return bool(re.search(r"(?im)^\s*-\s*Status:\s*BLOCKED\b", self.body))

	@property
	def automatic_done(self) -> bool:
		return self.complete or self.manual or self.blocked


def parse_goals(path: Path) -> list[Goal]:
	goals: list[Goal] = []
	current: tuple[str, str, str] | None = None
	body: list[str] = []
	for line in path.read_text(encoding="utf-8").splitlines():
		match = GOAL_RE.match(line)
		if match:
			if current:
				goals.append(Goal(*current, "\n".join(body).strip()))
			current = match.groups()
			body = []
		elif current:
			body.append(line)
	if current:
		goals.append(Goal(*current, "\n".join(body).strip()))
	return goals


def run(command: list[str], root: Path, *, capture: bool = False,
	env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
	print("$ " + " ".join(command), flush=True)
	return subprocess.run(command, cwd=root, text=True, check=False,
		capture_output=capture, env=env or os.environ.copy())


def git_context(root: Path) -> str:
	commands = (
		["git", "status", "--short", "--branch"],
		["git", "log", "-5", "--oneline"],
		["git", "submodule", "status", "--recursive"],
	)
	chunks: list[str] = []
	for command in commands:
		result = run(command, root, capture=True)
		chunks.append(f"$ {' '.join(command)}\n{result.stdout.strip()}")
	return "\n\n".join(chunks)


def api_models_url(api_base: str) -> str:
	parsed = urlparse(api_base)
	if parsed.path.rstrip("/").endswith("/v1"):
		return api_base.rstrip("/") + "/models"
	return api_base.rstrip("/") + "/v1/models"


def model_ready(api_base: str) -> bool:
	request = Request(api_models_url(api_base))
	if os.environ.get("OPENAI_API_KEY"):
		request.add_header("Authorization", f"Bearer {os.environ['OPENAI_API_KEY']}")
	try:
		with urlopen(request, timeout=3) as response:
			return 200 <= response.status < 500
	except (OSError, URLError):
		return False


def dolphin_executable() -> str:
	if os.environ.get("DOLPHIN_EXECUTABLE"):
		return os.environ["DOLPHIN_EXECUTABLE"]
	if shutil.which("dolphin-emu"):
		return shutil.which("dolphin-emu") or "dolphin-emu"
	if shutil.which("dolphin"):
		return shutil.which("dolphin") or "dolphin"
	flatpak = shutil.which("flatpak")
	flatpak_id = os.environ.get("DOLPHIN_FLATPAK_ID", "org.DolphinEmu.dolphin-emu")
	if flatpak:
		result = subprocess.run([flatpak, "info", flatpak_id],
			text=True, capture_output=True, check=False)
		if result.returncode == 0:
			return f"flatpak:{flatpak_id}"
	return "unavailable"


def automation_context() -> str:
	api_base = os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1")
	api_endpoint = "/models" if urlparse(api_base).path.rstrip("/").endswith("/v1") else "/v1/models"
	return "\n".join((
		f"MODEL_API=reachable OpenAI-compatible endpoint ({api_endpoint})",
		f"DOLPHIN_EXECUTABLE={dolphin_executable()}",
		f"DOLPHIN_FLATPAK_ID={os.environ.get('DOLPHIN_FLATPAK_ID', 'org.DolphinEmu.dolphin-emu')}",
	))


def run_dolphin_probe(root: Path) -> subprocess.CompletedProcess[str]:
	env = os.environ.copy()
	env.setdefault("DOLPHIN_TIMEOUT", os.environ.get("DOLPHIN_TIMEOUT", "600"))
	env.setdefault("DOLPHIN_EXECUTABLE", dolphin_executable())
	return run(["scripts/dolphin-boot-probe.sh"], root, capture=True, env=env)


def probe_log_dir(output: str) -> str | None:
	match = re.search(r"^Logs: (.+)$", output, re.MULTILINE)
	return match.group(1).strip() if match else None


def probe_log_tail(root: Path, log_dir: str | None, *, lines: int = 80) -> str:
	if not log_dir:
		return "(no log directory recorded)"
	stderr = root / log_dir / "stderr.log"
	if not stderr.is_file():
		return f"(missing {stderr})"
	content = stderr.read_text(encoding="utf-8", errors="replace").splitlines()
	return "\n".join(content[-lines:])


def clip_text(text: str, limit: int = 360) -> str:
	text = re.sub(r"\s+", " ", text).strip()
	if len(text) <= limit:
		return text
	return text[:limit - 3].rstrip() + "..."


def load_loop_memory(root: Path) -> dict[str, object]:
	path = root / MEMORY_FILE
	if not path.is_file():
		return {
			"version": 1,
			"task_goal": "Port Xash3D to run Half-Life 1 on native GameCube hardware.",
			"rooms": {},
			"past_tool_calls": [],
		}
	try:
		data = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError):
		data = {}
	if not isinstance(data, dict):
		data = {}
	data.setdefault("version", 1)
	data.setdefault("task_goal", "Port Xash3D to run Half-Life 1 on native GameCube hardware.")
	data.setdefault("rooms", {})
	data.setdefault("past_tool_calls", [])
	return data


def save_loop_memory(root: Path, memory: dict[str, object]) -> None:
	path = root / MEMORY_FILE
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(memory, indent=2, sort_keys=True) + "\n",
		encoding="utf-8")


def memory_room(memory: dict[str, object], goal: Goal) -> dict[str, object]:
	rooms = memory.setdefault("rooms", {})
	if not isinstance(rooms, dict):
		rooms = {}
		memory["rooms"] = rooms
	key = f"goal:{goal.goal_id}"
	room = rooms.setdefault(key, {
		"title": goal.title,
		"drawers": [],
		"investigative_gaps": [],
	})
	if not isinstance(room, dict):
		room = {"title": goal.title, "drawers": [], "investigative_gaps": []}
		rooms[key] = room
	room["title"] = goal.title
	room.setdefault("drawers", [])
	room.setdefault("investigative_gaps", [])
	return room


def fault_evidence(output: str, *, max_items: int = 8) -> list[dict[str, object]]:
	evidence: list[dict[str, object]] = []
	lines = output.splitlines()
	for pattern in FAULT_PATTERNS:
		regex = re.compile(pattern, re.IGNORECASE)
		for index, line in enumerate(lines, start=1):
			if regex.search(line):
				evidence.append({
					"pattern": pattern,
					"line": index,
					"text": clip_text(line),
				})
				break
		if len(evidence) >= max_items:
			break
	return evidence


def hypothesis_for(exit_code: int, evidence: list[dict[str, object]], phase: str) -> str:
	joined = " ".join(str(item.get("text", "")) for item in evidence).lower()
	if "token" in joined or "context" in joined:
		return "Model context or output budget constrained the previous attempt."
	if "aider made no edit" in joined:
		return "The model did not find or emit an applicable patch."
	if "could not load" in joined or "missing" in joined:
		return "The next blocker is likely asset lookup, staging, or path handling."
	if "_mem_alloc" in joined:
		return "The next blocker is likely memory pressure or an oversized cache/allocation."
	if "host_error" in joined or "sys_error" in joined or "fatal" in joined:
		return "The next blocker is a guest fatal path that needs the nearest log context."
	if "black screen" in joined or "diagnostic marker" in joined:
		return "The next blocker is likely visual output evidence or renderer presentation."
	if "audio" in joined:
		return "The next blocker is likely audio initialization, mixing, or output evidence."
	if exit_code == 0:
		return f"{phase} completed; preserve this evidence when deciding completion."
	return f"{phase} exited {exit_code}; inspect the cited evidence before retrying."


def investigative_gap(evidence: list[dict[str, object]], phase: str) -> str:
	if evidence:
		first = str(evidence[0].get("text", ""))
		return f"Read around the first `{phase}` evidence line: {clip_text(first, 160)}"
	return f"Find the first decisive error or missing evidence from the `{phase}` trace."


def recent_log_text(root: Path, pattern: str = "aider-pass-*.log", *,
	max_chars: int = 12000) -> tuple[str, str | None]:
	logs = sorted((root / ".ai/logs").glob(pattern), key=lambda path: path.stat().st_mtime)
	if not logs:
		return "", None
	path = logs[-1]
	text = path.read_text(encoding="utf-8", errors="replace")
	return text[-max_chars:], str(path.relative_to(root))


def record_investigation(memory: dict[str, object], goal: Goal, *, attempt: int,
	phase: str, exit_code: int, output: str = "", log_path: str | None = None) -> None:
	evidence = fault_evidence(output)
	room = memory_room(memory, goal)
	drawers = room.setdefault("drawers", [])
	if not isinstance(drawers, list):
		drawers = []
		room["drawers"] = drawers
	entry = {
		"timestamp": datetime.now().astimezone().isoformat(timespec="seconds"),
		"attempt": attempt,
		"phase": phase,
		"exit_code": exit_code,
		"log": log_path,
		"hypothesis": hypothesis_for(exit_code, evidence, phase),
		"evidence": evidence,
		"gap": investigative_gap(evidence, phase),
	}
	drawers.append(entry)
	del drawers[:-MEMORY_MAX_DRAWERS]

	gaps = room.setdefault("investigative_gaps", [])
	if isinstance(gaps, list):
		gaps.append(entry["gap"])
		del gaps[:-MEMORY_MAX_DRAWERS]

	tool_calls = memory.setdefault("past_tool_calls", [])
	if isinstance(tool_calls, list):
		tool_calls.append({
			"timestamp": entry["timestamp"],
			"goal": goal.goal_id,
			"phase": phase,
			"exit_code": exit_code,
			"log": log_path,
		})
		del tool_calls[:-MEMORY_MAX_TOOL_CALLS]


def memory_summary(memory: dict[str, object], goal: Goal) -> str:
	room = memory_room(memory, goal)
	drawers = room.get("drawers", [])
	tool_calls = memory.get("past_tool_calls", [])
	lines = [
		f"Underlying task goal: {memory.get('task_goal', '')}",
		"Goal hypotheses and evidence:",
	]
	if isinstance(drawers, list) and drawers:
		for entry in drawers[-4:]:
			evidence = entry.get("evidence", []) if isinstance(entry, dict) else []
			if isinstance(evidence, list) and evidence:
				ev = "; ".join(clip_text(str(item.get("text", "")), 120)
					for item in evidence[:3] if isinstance(item, dict))
			else:
				ev = "(no decisive evidence captured yet)"
			lines.append(
				f"- attempt {entry.get('attempt')} {entry.get('phase')} exit {entry.get('exit_code')}: "
				f"{entry.get('hypothesis')} Evidence: {ev}"
			)
	else:
		lines.append("- No prior dynamic memory for this goal.")
	gaps = room.get("investigative_gaps", [])
	if isinstance(gaps, list) and gaps:
		lines.append("Investigative gaps:")
		for gap in gaps[-3:]:
			lines.append(f"- {gap}")
	if isinstance(tool_calls, list) and tool_calls:
		lines.append("Recent investigation tool calls:")
		for call in tool_calls[-5:]:
			if isinstance(call, dict):
				lines.append(
					f"- {call.get('goal')} {call.get('phase')} exit {call.get('exit_code')}"
					f"{' -> ' + call['log'] if call.get('log') else ''}"
				)
	return "\n".join(lines)


def insert_goal_evidence(text: str, goal_id: str, evidence_lines: list[str]) -> str:
	header_re = re.compile(rf"^(## {re.escape(goal_id)} \[)( \])", re.MULTILINE)
	match = header_re.search(text)
	if not match:
		return text
	text = header_re.sub(r"\1x]", text, count=1)
	insert_at = match.end()
	next_header = re.search(r"^## G\d+", text[insert_at:], re.MULTILINE)
	section_end = insert_at + next_header.start() if next_header else len(text)
	stamp = datetime.now().astimezone().strftime("%Y-%m-%d")
	block = "".join(f"\n- Verified {stamp}: {line}" for line in evidence_lines)
	return text[:section_end].rstrip() + block + "\n" + text[section_end:]


def commit_probe_success(root: Path, goal: Goal, log_dir: str, probe_output: str) -> int:
	goal_path = root / ".ai/goals/GAMECUBE_PORT_GOALS.md"
	plan_path = root / "docs/GAMECUBE_PORT_PLAN.md"
	timeout = os.environ.get("DOLPHIN_TIMEOUT", "600")
	summary = probe_output.splitlines()[0] if probe_output else "MAP_READY"
	goal_path.write_text(insert_goal_evidence(
		goal_path.read_text(encoding="utf-8"),
		goal.goal_id,
		[
			f"`DOLPHIN_TIMEOUT={timeout} scripts/dolphin-boot-probe.sh`",
			f"Result: {summary}",
			f"Evidence: `{log_dir}/stderr.log`",
		],
	), encoding="utf-8")
	plan_note = (
		f"\n\n### {goal.goal_id} probe pass "
		f"({datetime.now().astimezone().strftime('%Y-%m-%d')})\n"
		f"- Command: `DOLPHIN_TIMEOUT={timeout} scripts/dolphin-boot-probe.sh`\n"
		f"- Result: `{summary}`\n"
		f"- Logs: `{log_dir}/stderr.log`\n"
	)
	plan_path.write_text(plan_path.read_text(encoding="utf-8").rstrip() + plan_note + "\n",
		encoding="utf-8")
	subject = GOAL_COMMIT_SUBJECT.get(goal.goal_id,
		f"test: complete GameCube goal {goal.goal_id}")
	add = run(["git", "add", str(goal_path.relative_to(root)),
		str(plan_path.relative_to(root))], root)
	if add.returncode != 0:
		return add.returncode
	return run(["git", "commit", "-m", subject], root).returncode


def task_for(goal: Goal, root: Path, attempt: int, investigation_memory: str,
	probe_result: tuple[int, str, str | None] | None = None) -> str:
	retry_instruction = ""
	if attempt > 1:
		retry_instruction = (
			"Previous attempt did not produce an accepted commit. Make a concrete "
			"smallest safe patch; do not ask for context.\n\n"
		)
	if attempt > 2:
		retry_instruction = (
			"Previous attempts hit an automation recovery path. Keep this pass surgical: "
			"prefer one source file plus the ledger/plan, avoid broad rewrites, and keep "
			"the response short enough to fit a reduced output budget. If the source file "
			"needed for the real fix is not loaded, update the goal ledger and port plan "
			"with the exact next file or blocker instead of stopping.\n\n"
		)
	probe_section = ""
	if probe_result is not None:
		exit_code, output, log_dir = probe_result
		probe_section = f"""
Dolphin boot probe (just executed):
- Exit code: {exit_code}
- Summary: {output.splitlines()[0] if output else '(empty)'}
- Logs: {log_dir or '(unknown)'}

Last stderr lines:
{probe_log_tail(root, log_dir)}

The probe did not satisfy `{goal.goal_id}`. Fix the guest-engine or probe
blocker shown above. Do not mark `{goal.goal_id}` complete and do not write
docs-only status updates when the probe still fails.
"""
	elif goal.goal_id in DOLPHIN_PROBE_GOALS:
		probe_section = (
			f"\nThis goal requires Dolphin runtime evidence. The goal runner executes "
			f"`scripts/dolphin-boot-probe.sh` before each pass; do not claim completion "
			f"without a successful probe result in the ledger.\n"
		)
	return f"""You are autonomously advancing the native Xash3D GameCube port.

Active goal: {goal.goal_id} — {goal.title}
Attempt on this goal: {attempt}

{retry_instruction}{probe_section}
Acceptance criteria:
{goal.body}

Repository context:
{git_context(root)}

Automation environment:
{automation_context()}

Investigation memory:
{investigation_memory}

Make one coherent patch using the preloaded files. Preserve non-GameCube
targets. Do not ask questions, propose commands, or stop at a plan. If the
premise is disproven, update the goal ledger and port plan with the blocker
instead of forcing an engine change.
Do not narrate your investigation. Emit only the Aider edit blocks needed for
the patch. Keep the reply under one file when possible and avoid repeating the
same SEARCH/REPLACE block.

Rules:
- Keep the commit below 400 changed lines and do not delete tracked files.
- Update `docs/GAMECUBE_PORT_PLAN.md` with commands and concrete evidence.
- If marking `{goal.goal_id}` done, include a command, result, and log path or
  runtime artifact in the goal notes and port plan; docs-only reasoning is not
  enough for completion.
- Update this goal's notes when useful.
- Mark `{goal.goal_id}` done only when every acceptance criterion is demonstrated.
  Otherwise leave it unchecked and state the next blocker in the port plan.
- Never mark MANUAL goals complete.
- Use the investigation memory as prior evidence, but verify stale hypotheses
  against current source, logs, or artifacts before acting on them.
- Stop after this coherent patch; the goal runner decides what comes next.
"""


def write_state(path: Path, **values: object) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	values["updated_at"] = datetime.now().astimezone().isoformat(timespec="seconds")
	path.write_text(json.dumps(values, indent=2) + "\n", encoding="utf-8")


def git_head(root: Path) -> str:
	return subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
		text=True, capture_output=True, check=False).stdout.strip()


def git_dirty(root: Path) -> bool:
	return bool(subprocess.run(["git", "status", "--porcelain"], cwd=root,
		text=True, capture_output=True, check=False).stdout.strip())


def clean_commit_advances_goal(root: Path, before: str, after: str, expected_subject: str) -> bool:
	if not before or not after or before == after or git_dirty(root):
		return False
	subject = subprocess.run(["git", "log", "-1", "--format=%s", after], cwd=root,
		text=True, capture_output=True, check=False).stdout.strip()
	if subject != expected_subject:
		return False
	changed = subprocess.run(["git", "diff", "--name-only", f"{before}..{after}"],
		cwd=root, text=True, capture_output=True, check=False).stdout.splitlines()
	return "docs/GAMECUBE_PORT_PLAN.md" in changed


def dirty_commit_subject(goal_id: str | None = None) -> str:
	if goal_id:
		return f"chore: checkpoint automation state before {goal_id}"
	return "chore: checkpoint dirty automation state"


def commit_dirty_worktree(root: Path, goal_id: str | None = None) -> int:
	if not git_dirty(root):
		return 0
	subject = dirty_commit_subject(goal_id)
	print(f"goal-loop: dirty worktree detected; creating checkpoint commit: {subject}",
		file=sys.stderr, flush=True)
	add = run(["git", "add", "-A"], root)
	if add.returncode != 0:
		return add.returncode
	commit = run(["git", "commit", "-m", subject], root)
	return commit.returncode


def context_for_goal(goal_id: str, root: Path, attempt: int) -> list[str]:
	"""Return a progressively smaller editable context for recovery retries."""
	candidates: list[str] = []
	seen: set[str] = set()
	for path in (*COMMON_CONTEXT, *GOAL_CONTEXT.get(goal_id, ())):
		if path in seen or not (root / path).is_file():
			continue
		seen.add(path)
		candidates.append(path)
	size_limit = (
		45000 if attempt == 1 else
		20000 if attempt == 2 else
		12000 if attempt == 3 else
		8000
	)
	required = set(COMMON_CONTEXT if attempt <= 2 else (".ai/goals/GAMECUBE_PORT_GOALS.md",))
	selected: list[str] = []
	for path in candidates:
		file_path = root / path
		if path in required or file_path.stat().st_size <= size_limit:
			selected.append(path)
	if not selected:
		return candidates[:1]
	return selected


def read_context_for_goal(goal_id: str, root: Path, attempt: int = 1) -> list[str]:
	"""Return focused read-only notes for the active goal."""
	if os.environ.get("AIDER_AUTOMATION", "1") == "1" and attempt >= 3:
		return []
	common_reads = () if os.environ.get("AIDER_AUTOMATION", "1") == "1" else COMMON_READ_CONTEXT
	seen: set[str] = set()
	selected: list[str] = []
	for path in (*common_reads, *GOAL_READ_CONTEXT.get(goal_id, ())):
		if path in seen or not (root / path).is_file():
			continue
		seen.add(path)
		selected.append(f"read:{path}")
	return selected


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--max-passes", type=int, default=20)
	parser.add_argument("--recoverable-retries", type=int, default=8,
		help="retry one goal this many times for token/timeout/no-edit failures")
	parser.add_argument("--list", action="store_true", help="print goal state and exit")
	parser.add_argument("--status-json", action="store_true", help="emit machine-readable goal state")
	args = parser.parse_args()
	root = args.repo.expanduser().resolve()
	goal_file = root / ".ai/goals/GAMECUBE_PORT_GOALS.md"
	state_file = root / ".ai/logs/goal-loop-state.json"
	memory = load_loop_memory(root)
	interrupted_signal = 0
	load_dotenv(root / ".env")

	def stop_cleanly(signum: int, _frame: object) -> None:
		nonlocal interrupted_signal
		interrupted_signal = signum
		write_state(state_file, state="stopped", signal=signum,
			message="Automation stopped by operator")
		print("\nGoal automation stopped by operator.", file=sys.stderr, flush=True)
		raise KeyboardInterrupt

	signal.signal(signal.SIGTERM, stop_cleanly)
	signal.signal(signal.SIGINT, stop_cleanly)
	if not goal_file.is_file():
		parser.error(f"goal file not found: {goal_file}")

	goals = parse_goals(goal_file)
	if args.status_json:
		print(json.dumps([asdict(goal) | {"complete": goal.complete,
			"manual": goal.manual, "blocked": goal.blocked} for goal in goals]))
		return 0
	if args.list:
		for goal in goals:
			state = "manual" if goal.manual else "blocked" if goal.blocked \
				else "complete" if goal.complete else "pending"
			print(f"{goal.goal_id}\t{state}\t{goal.title}")
		return 0
	if args.max_passes < 1:
		parser.error("--max-passes must be positive")
	if commit_dirty_worktree(root) != 0:
		print("goal-loop: failed to checkpoint the dirty worktree", file=sys.stderr)
		return 2
	if not os.environ.get("OPENAI_API_KEY"):
		print("goal-loop: OPENAI_API_KEY must be supplied by the launch environment", file=sys.stderr)
		return 2
	api_base = os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1")
	if not model_ready(api_base):
		print(f"goal-loop: model API is not reachable at {api_base}", file=sys.stderr)
		print("Start the Qwable/vLLM server first, then retry.", file=sys.stderr)
		return 2

	attempts: dict[str, int] = {}
	for pass_index in range(1, args.max_passes + 1):
		goals = parse_goals(goal_file)
		goal = next((item for item in goals if not item.automatic_done), None)
		if goal is None:
			write_state(state_file, state="complete", pass_index=pass_index - 1,
				message="All automatic goals are complete or blocked")
			print("All automatic GameCube port goals are complete or blocked.")
			return 0
		attempts[goal.goal_id] = attempts.get(goal.goal_id, 0) + 1
		if commit_dirty_worktree(root, goal.goal_id) != 0:
			write_state(state_file, state="failed", pass_index=pass_index,
				goal=asdict(goal), attempt=attempts[goal.goal_id],
				message="failed to checkpoint dirty worktree")
			return 2
		print(f"\n{'=' * 72}\nGOAL PASS {pass_index}/{args.max_passes}: "
			f"{goal.goal_id} — {goal.title}\n{'=' * 72}", flush=True)
		write_state(state_file, state="running", pass_index=pass_index,
			goal=asdict(goal), attempt=attempts[goal.goal_id])
		probe_result: tuple[int, str, str | None] | None = None
		if goal.goal_id in DOLPHIN_PROBE_GOALS:
			print(f"\n--- Dolphin boot probe for {goal.goal_id} ---", flush=True)
			probe = run_dolphin_probe(root)
			probe_output = ((probe.stdout or "") + (probe.stderr or "")).strip()
			log_dir = probe_log_dir(probe_output)
			print(probe_output, flush=True)
			success_marker = PROBE_AUTO_COMPLETE.get(goal.goal_id)
			if probe.returncode == 0 and success_marker and success_marker in probe_output:
				record_investigation(memory, goal, attempt=attempts[goal.goal_id],
					phase="dolphin-probe", exit_code=probe.returncode,
					output=probe_output, log_path=f"{log_dir}/stderr.log" if log_dir else None)
				save_loop_memory(root, memory)
				if commit_probe_success(root, goal, log_dir or "", probe_output) != 0:
					write_state(state_file, state="failed", pass_index=pass_index,
						goal=asdict(goal), message="probe succeeded but commit failed")
					return 1
				review = run(["scripts/ai-review.sh"], root)
				if review.returncode != 0:
					write_state(state_file, state="failed-review", pass_index=pass_index,
						goal=asdict(goal), exit_code=review.returncode)
					return review.returncode
				print(f"{goal.goal_id}: probe satisfied acceptance criteria; continuing.",
					flush=True)
				continue
			probe_result = (probe.returncode, probe_output, log_dir)
			record_investigation(memory, goal, attempt=attempts[goal.goal_id],
				phase="dolphin-probe", exit_code=probe.returncode,
				output=probe_output, log_path=f"{log_dir}/stderr.log" if log_dir else None)
			save_loop_memory(root, memory)
		with tempfile.NamedTemporaryFile("w", suffix=".md", prefix="xash3d-gc-goal-",
			encoding="utf-8", delete=False) as task:
			task.write(task_for(goal, root, attempts[goal.goal_id],
				memory_summary(memory, goal), probe_result))
			task_path = Path(task.name)
		head_before = git_head(root)
		try:
			context_files = context_for_goal(goal.goal_id, root, attempts[goal.goal_id])
			read_context_files = read_context_for_goal(goal.goal_id, root, attempts[goal.goal_id])
			pass_env = os.environ.copy()
			expected_subject = GOAL_COMMIT_SUBJECT.get(goal.goal_id,
				f"feat: advance GameCube port goal {goal.goal_id}")
			pass_env["AI_COMMIT_SUBJECT"] = expected_subject
			pass_env["AI_DIRTY_COMMIT_SUBJECT"] = dirty_commit_subject(goal.goal_id)
			pass_env["AIDER_BUDGET_ATTEMPT"] = str(attempts[goal.goal_id])
			pass_env.setdefault("AIDER_AUTOMATION", "1")
			if attempts[goal.goal_id] >= 3:
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_INITIAL", "1024")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_1", "768")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_2", "512")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_3", "384")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_INITIAL", "20000")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_RETRY_1", "12000")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_RETRY_2", "8000")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_RETRY_3", "6000")
			result = run(["scripts/ai-aider-pass.sh", str(root), str(task_path),
				*context_files, *read_context_files], root, env=pass_env)
		finally:
			task_path.unlink(missing_ok=True)
		if result.returncode != 0:
			log_tail, log_path = recent_log_text(root)
			record_investigation(memory, goal, attempt=attempts[goal.goal_id],
				phase="aider-pass", exit_code=result.returncode,
				output=log_tail, log_path=log_path)
			save_loop_memory(root, memory)
			head_after = git_head(root)
			if clean_commit_advances_goal(root, head_before, head_after, expected_subject):
				write_state(state_file, state="resuming-after-commit", pass_index=pass_index,
					goal=asdict(goal), attempt=attempts[goal.goal_id],
					exit_code=result.returncode,
					message="Child pass exited nonzero after creating a clean commit; reviewing and continuing")
				print("Child pass exited nonzero after a clean commit; reviewing and continuing.",
					file=sys.stderr)
				review = run(["scripts/ai-review.sh"], root)
				if review.returncode != 0:
					write_state(state_file, state="failed-review", pass_index=pass_index,
						goal=asdict(goal), exit_code=review.returncode)
					return review.returncode
				continue
			if head_after and head_after != head_before:
				write_state(state_file, state="recovering-after-unrelated-commit",
					pass_index=pass_index, goal=asdict(goal),
					attempt=attempts[goal.goal_id], exit_code=result.returncode,
					message="HEAD moved, but not with the expected goal commit; treating child failure as recoverable when possible")
				print("HEAD moved without an accepted goal commit; continuing recovery logic.",
					file=sys.stderr)
			if result.returncode in RECOVERABLE_EXIT_CODES and \
					attempts[goal.goal_id] <= args.recoverable_retries:
				reason = RECOVERABLE_EXIT_CODES[result.returncode]
				write_state(state_file, state="recovering", pass_index=pass_index,
					goal=asdict(goal), attempt=attempts[goal.goal_id],
					exit_code=result.returncode, message=f"{reason}; retrying goal")
				print(f"{reason}; retrying this goal with a tighter context.",
					file=sys.stderr)
				continue
			write_state(state_file, state="failed", pass_index=pass_index,
				goal=asdict(goal), exit_code=result.returncode)
			return result.returncode
		log_tail, log_path = recent_log_text(root)
		record_investigation(memory, goal, attempt=attempts[goal.goal_id],
			phase="aider-pass", exit_code=result.returncode,
			output=log_tail, log_path=log_path)
		save_loop_memory(root, memory)
		review = run(["scripts/ai-review.sh"], root)
		if review.returncode != 0:
			record_investigation(memory, goal, attempt=attempts[goal.goal_id],
				phase="review", exit_code=review.returncode,
				output=f"scripts/ai-review.sh exited {review.returncode}")
			save_loop_memory(root, memory)
			write_state(state_file, state="failed-review", pass_index=pass_index,
				goal=asdict(goal), exit_code=review.returncode)
			return review.returncode

	write_state(state_file, state="pass-limit", pass_index=args.max_passes,
		message="Pass limit reached with automatic goals remaining")
	print("Goal pass limit reached; stopping for human review.", file=sys.stderr)
	return 3


if __name__ == "__main__":
	try:
		raise SystemExit(main())
	except KeyboardInterrupt:
		raise SystemExit(130) from None
