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

GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |x|X|MANUAL)\]\s+(.+)$")
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
	return "\n".join((
		f"OPENAI_API_BASE={os.environ.get('OPENAI_API_BASE', 'http://127.0.0.1:8072/v1')}",
		f"DOLPHIN_EXECUTABLE={dolphin_executable()}",
		f"DOLPHIN_FLATPAK_ID={os.environ.get('DOLPHIN_FLATPAK_ID', 'org.DolphinEmu.dolphin-emu')}",
	))


def task_for(goal: Goal, root: Path, attempt: int) -> str:
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
	return f"""You are autonomously advancing the native Xash3D GameCube port.

Active goal: {goal.goal_id} — {goal.title}
Attempt on this goal: {attempt}

{retry_instruction}Acceptance criteria:
{goal.body}

Repository context:
{git_context(root)}

Automation environment:
{automation_context()}

Make one coherent patch using the preloaded files. Preserve non-GameCube
targets. Do not ask questions, propose commands, or stop at a plan. If the
premise is disproven, update the goal ledger and port plan with the blocker
instead of forcing an engine change.
Do not narrate your investigation. Emit only the Aider edit blocks needed for
the patch.

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


def context_for_goal(goal_id: str, root: Path, attempt: int) -> list[str]:
	"""Return a progressively smaller editable context for recovery retries."""
	candidates: list[str] = []
	seen: set[str] = set()
	for path in (*COMMON_CONTEXT, *GOAL_CONTEXT.get(goal_id, ())):
		if path in seen or not (root / path).is_file():
			continue
		seen.add(path)
		candidates.append(path)
	size_limit = 45000 if attempt == 1 else 20000 if attempt == 2 else 12000
	required = set(COMMON_CONTEXT if attempt <= 2 else (".ai/goals/GAMECUBE_PORT_GOALS.md",))
	selected: list[str] = []
	for path in candidates:
		file_path = root / path
		if path in required or file_path.stat().st_size <= size_limit:
			selected.append(path)
	if not selected:
		return candidates[:1]
	return selected


def read_context_for_goal(goal_id: str, root: Path) -> list[str]:
	"""Return focused read-only notes for the active goal."""
	seen: set[str] = set()
	selected: list[str] = []
	for path in (*COMMON_READ_CONTEXT, *GOAL_READ_CONTEXT.get(goal_id, ())):
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
	if subprocess.run(["git", "status", "--porcelain"], cwd=root,
		capture_output=True, text=True).stdout.strip():
		print("goal-loop: refusing to start with a dirty worktree", file=sys.stderr)
		return 2
	if not os.environ.get("OPENAI_API_KEY"):
		print("goal-loop: OPENAI_API_KEY must be supplied by the launch environment", file=sys.stderr)
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
		print(f"\n{'=' * 72}\nGOAL PASS {pass_index}/{args.max_passes}: "
			f"{goal.goal_id} — {goal.title}\n{'=' * 72}", flush=True)
		write_state(state_file, state="running", pass_index=pass_index,
			goal=asdict(goal), attempt=attempts[goal.goal_id])
		with tempfile.NamedTemporaryFile("w", suffix=".md", prefix="xash3d-gc-goal-",
			encoding="utf-8", delete=False) as task:
			task.write(task_for(goal, root, attempts[goal.goal_id]))
			task_path = Path(task.name)
		head_before = git_head(root)
		try:
			context_files = context_for_goal(goal.goal_id, root, attempts[goal.goal_id])
			read_context_files = read_context_for_goal(goal.goal_id, root)
			pass_env = os.environ.copy()
			pass_env["AI_COMMIT_SUBJECT"] = GOAL_COMMIT_SUBJECT.get(goal.goal_id,
				f"feat: advance GameCube port goal {goal.goal_id}")
			if attempts[goal.goal_id] >= 3:
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_INITIAL", "1024")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_1", "768")
				pass_env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_2", "512")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_INITIAL", "20000")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_RETRY_1", "12000")
				pass_env.setdefault("AIDER_CONTEXT_BYTES_RETRY_2", "8000")
			result = run(["scripts/ai-aider-pass.sh", str(root), str(task_path),
				*context_files, *read_context_files], root, env=pass_env)
		finally:
			task_path.unlink(missing_ok=True)
		if result.returncode != 0:
			head_after = git_head(root)
			if head_after and head_after != head_before and not git_dirty(root):
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
		review = run(["scripts/ai-review.sh"], root)
		if review.returncode != 0:
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
