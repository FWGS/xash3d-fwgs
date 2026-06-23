#!/usr/bin/env python3
"""Reject completed GameCube goals that lack recorded evidence."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |x|X|MANUAL)\]\s+(.+)$")
EVIDENCE_RE = re.compile(
	r"(?i)(\.ai/logs/|Evidence:|Verified\s+\d{4}-\d{2}-\d{2}|"
	r"DOLPHIN_TIMEOUT=|scripts/[\w./-]+|Result:)"
)


@dataclass
class Goal:
	goal_id: str
	state: str
	title: str
	body: str

	@property
	def complete(self) -> bool:
		return self.state.lower() == "x"


def git_text(root: Path, ref: str, path: str) -> str:
	result = subprocess.run(["git", "show", f"{ref}:{path}"], cwd=root,
		text=True, capture_output=True, check=False)
	return result.stdout if result.returncode == 0 else ""


def parse_goals(text: str) -> dict[str, Goal]:
	goals: dict[str, Goal] = {}
	current: tuple[str, str, str] | None = None
	body: list[str] = []
	for line in text.splitlines():
		match = GOAL_RE.match(line)
		if match:
			if current:
				goals[current[0]] = Goal(*current, "\n".join(body).strip())
			current = match.groups()
			body = []
		elif current:
			body.append(line)
	if current:
		goals[current[0]] = Goal(*current, "\n".join(body).strip())
	return goals


def changed_completed_goals(root: Path, baseline: str) -> list[Goal]:
	before = parse_goals(git_text(root, baseline, ".ai/goals/GAMECUBE_PORT_GOALS.md"))
	after = parse_goals((root / ".ai/goals/GAMECUBE_PORT_GOALS.md").read_text(encoding="utf-8"))
	completed: list[Goal] = []
	for goal_id, goal in after.items():
		was_complete = before.get(goal_id, Goal(goal_id, " ", "", "")).complete
		if goal.complete and not was_complete:
			completed.append(goal)
	return completed


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("baseline", help="commit before the autonomous patch")
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	args = parser.parse_args()
	root = args.repo.resolve()

	completed = changed_completed_goals(root, args.baseline)
	if not completed:
		print("evidence-gate: no newly completed goals")
		return 0

	plan = (root / "docs/GAMECUBE_PORT_PLAN.md").read_text(encoding="utf-8")
	failures: list[str] = []
	for goal in completed:
		plan_mentions_goal = goal.goal_id in plan or goal.title in plan
		if not EVIDENCE_RE.search(goal.body) or not plan_mentions_goal:
			failures.append(f"{goal.goal_id} {goal.title}")

	if failures:
		print("evidence-gate: newly completed goals lack command/log evidence:", file=sys.stderr)
		for item in failures:
			print(f"  - {item}", file=sys.stderr)
		print("Add concrete command output, log path, or verification result to the goal and port plan.",
			file=sys.stderr)
		return 1

	print("evidence-gate: completed goals have recorded evidence")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
