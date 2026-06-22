#!/usr/bin/env python3
"""Advance the Xash3D GameCube port through evidence-gated Aider goals."""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
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
	"G13": ("scripts/dolphin-boot-probe.sh", "scripts/build-gamecube-disc.py",
		"engine/platform/gamecube/sys_gamecube.c"),
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
	"G13": "test: smoke GameCube map loading",
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


def task_for(goal: Goal, root: Path, attempt: int) -> str:
	retry_instruction = ""
	if attempt > 1:
		retry_instruction = (
			"Previous attempt made no edit. Make a concrete smallest safe patch; "
			"do not ask for context.\n\n"
		)
	return f"""You are autonomously advancing the native Xash3D GameCube port.

Active goal: {goal.goal_id} — {goal.title}
Attempt on this goal: {attempt}

{retry_instruction}Acceptance criteria:
{goal.body}

Repository context:
{git_context(root)}

Make one coherent patch using the preloaded files. Preserve non-GameCube
targets. Do not ask questions, propose commands, or stop at a plan. If the
premise is disproven, update the goal ledger and port plan with the blocker
instead of forcing an engine change.
Do not narrate your investigation. Emit only the Aider edit blocks needed for
the patch.

Rules:
- Keep the commit below 400 changed lines and do not delete tracked files.
- Update `docs/GAMECUBE_PORT_PLAN.md` with commands and concrete evidence.
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


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--max-passes", type=int, default=20)
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
		try:
			context_files = [path for path in (*COMMON_CONTEXT,
				*GOAL_CONTEXT.get(goal.goal_id, ()))
				if (root / path).is_file()]
			pass_env = os.environ.copy()
			pass_env["AI_COMMIT_SUBJECT"] = GOAL_COMMIT_SUBJECT.get(goal.goal_id,
				f"feat: advance GameCube port goal {goal.goal_id}")
			result = run(["scripts/ai-aider-pass.sh", str(root), str(task_path),
				*context_files], root, env=pass_env)
		finally:
			task_path.unlink(missing_ok=True)
		if result.returncode != 0:
			if result.returncode == 10 and attempts[goal.goal_id] < 2:
				write_state(state_file, state="retrying-no-edit", pass_index=pass_index,
					goal=asdict(goal), attempt=attempts[goal.goal_id],
					message="Aider made no edit; retrying goal once")
				print("Aider made no edit; retrying this goal once.", file=sys.stderr)
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
