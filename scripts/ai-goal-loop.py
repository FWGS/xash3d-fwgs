#!/usr/bin/env python3
"""Advance the Xash3D GameCube port through evidence-gated Aider goals."""

from __future__ import annotations

import argparse
import json
import os
import re
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
	"G01": ("engine/server/sv_game.c", "engine/server/server.h"),
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
	"G07": ("engine/platform/gamecube/sys_gamecube.c", "engine/server/sv_init.c",
		"ref/gx/r_main.c"),
}


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


def run(command: list[str], root: Path, *, capture: bool = False) -> subprocess.CompletedProcess[str]:
	print("$ " + " ".join(command), flush=True)
	return subprocess.run(command, cwd=root, text=True, check=False,
		capture_output=capture, env=os.environ.copy())


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
	return f"""You are autonomously advancing the native Xash3D GameCube port.

Active goal: {goal.goal_id} — {goal.title}
Attempt on this goal: {attempt}

Acceptance criteria:
{goal.body}

Repository context:
{git_context(root)}

Read `.ai/goals/GAMECUBE_PORT_GOALS.md`, `docs/GAMECUBE_PORT_PLAN.md`, the
latest diff, and the configured project rules. Make one coherent patch that
materially advances this goal. Diagnose before editing and preserve all
non-GameCube targets.

The harness has preloaded the goal-relevant source files into Aider. Inspect
them directly and use the repository map for related symbols. Do not stop to
ask the user to add files that already exist in this checkout.

Rules:
- Keep the commit below 400 changed lines and do not delete tracked files.
- Update `docs/GAMECUBE_PORT_PLAN.md` with commands and concrete evidence.
- Update this goal's notes when useful.
- Change `{goal.goal_id} [ ]` to `{goal.goal_id} [x]` only when every acceptance
  criterion is actually demonstrated. Otherwise leave it unchecked and state
  the next blocker in the port plan.
- Never mark MANUAL goals complete.
- Run focused checks. The harness will run the full verifier and review gate.
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
	if not goal_file.is_file():
		parser.error(f"goal file not found: {goal_file}")

	goals = parse_goals(goal_file)
	if args.status_json:
		print(json.dumps([asdict(goal) | {"complete": goal.complete, "manual": goal.manual} for goal in goals]))
		return 0
	if args.list:
		for goal in goals:
			state = "manual" if goal.manual else "complete" if goal.complete else "pending"
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
		goal = next((item for item in goals if not item.complete and not item.manual), None)
		if goal is None:
			write_state(state_file, state="complete", pass_index=pass_index - 1,
				message="All automatic goals are complete")
			print("All automatic GameCube port goals are complete.")
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
			result = run(["scripts/ai-aider-pass.sh", str(root), str(task_path),
				*context_files], root)
		finally:
			task_path.unlink(missing_ok=True)
		if result.returncode != 0:
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
	raise SystemExit(main())
