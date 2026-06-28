#!/usr/bin/env python3
"""Validate the GameCube goal ledger shape used by the automation."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


GOAL_HEADER_RE = re.compile(r"^##\s+(G\d+)\b(?P<rest>.*)$")
VALID_GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |~|x|X|MANUAL)\]\s+(.+)$")


@dataclass
class Finding:
    line: int
    message: str


def validate_goals(path: Path) -> list[Finding]:
    findings: list[Finding] = []
    seen: dict[str, int] = {}
    text = path.read_text(encoding="utf-8")
    for lineno, line in enumerate(text.splitlines(), 1):
        header = GOAL_HEADER_RE.match(line)
        if not header:
            continue
        valid = VALID_GOAL_RE.match(line)
        goal_id = header.group(1)
        if not valid:
            findings.append(Finding(
                lineno,
                f"{goal_id} header is invisible to the goal runner; use [ ], [~], [x], or [MANUAL]",
            ))
            continue
        if goal_id in seen:
            findings.append(Finding(lineno, f"{goal_id} duplicates line {seen[goal_id]}"))
        seen[goal_id] = lineno
        state = valid.group(2).lower()
        title = valid.group(3).lower()
        if state == "x" and any(word in title for word in ("blocked", "pending", "manual required")):
            findings.append(Finding(
                lineno,
                f"{goal_id} is marked complete but its title still says blocked/pending/manual",
            ))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    args = parser.parse_args()

    path = args.repo / ".ai/goals/GAMECUBE_PORT_GOALS.md"
    findings = validate_goals(path)
    if findings:
        for finding in findings:
            print(f"FAIL: {path}:{finding.line}: {finding.message}", file=sys.stderr)
        return 1
    print(f"PASS: goal ledger shape - {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
