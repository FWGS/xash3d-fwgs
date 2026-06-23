#!/usr/bin/env python3
"""Check that the GameCube homebrew compliance profile is wired into the port."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Check:
	name: str
	ok: bool
	detail: str
	required: bool = True


def read(path: Path) -> str:
	return path.read_text(encoding="utf-8") if path.is_file() else ""


def git_grep(root: Path, pattern: str) -> bool:
	result = subprocess.run(["git", "grep", "-n", "-E", pattern, "--", "."],
		cwd=root, text=True, capture_output=True, check=False)
	return result.returncode == 0


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--strict", action="store_true",
		help="require release/hardware evidence, not just development wiring")
	args = parser.parse_args()
	root = args.repo.resolve()

	prompt = root / ".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"
	doc = root / "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"
	context_index = root / ".ai/prompts/GAMECUBE_CONTEXT_INDEX.md"
	aider_config = root / ".aider.conf.yml"
	port_plan = root / "docs/GAMECUBE_PORT_PLAN.md"
	goals = root / ".ai/goals/GAMECUBE_PORT_GOALS.md"

	prompt_text = read(prompt)
	doc_text = read(doc)
	index_text = read(context_index)
	config_text = read(aider_config)
	plan_text = read(port_plan)
	goals_text = read(goals)

	checks = [
		Check("prompt context", bool(prompt_text), str(prompt)),
		Check("human checklist", bool(doc_text), str(doc)),
		Check("context index references compliance",
			"GAMECUBE_HOMEBREW_COMPLIANCE.md" in index_text, str(context_index)),
		Check("aider reads compliance prompt",
			"GAMECUBE_HOMEBREW_COMPLIANCE.md" in config_text, str(aider_config)),
		Check("port plan references compliance",
			"GameCube Homebrew Compliance" in plan_text, str(port_plan)),
		Check("release goals mention compliance",
			bool(re.search(r"G4[12].*compliance|compliance.*G4[12]", goals_text,
				re.IGNORECASE | re.DOTALL)), str(goals)),
		Check("no obvious proprietary SDK text in tracked files",
			not git_grep(root, r"(Dolphin SDK|Revolution SDK|official Nintendo SDK)"),
			"git grep proprietary SDK strings"),
	]

	if args.strict:
		strict_needles = (
			"Boots on real hardware",
			"Swiss",
			"Memory Card",
			"Release checksum",
		)
		for needle in strict_needles:
			checks.append(Check(f"strict evidence: {needle}",
				needle in plan_text or needle in goals_text, needle))

	failed_required = False
	for check in checks:
		status = "PASS" if check.ok else "WARN" if not check.required else "FAIL"
		print(f"{status}: {check.name} - {check.detail}")
		if check.required and not check.ok:
			failed_required = True

	if failed_required:
		return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
