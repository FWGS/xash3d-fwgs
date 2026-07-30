#!/usr/bin/env python3
"""Generate a compact GameCube harness incident packet from the latest runtime evidence."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass(frozen=True)
class ActionRule:
    classification: str
    next_actions: tuple[str, ...]
    focus_files: tuple[str, ...]


ACTION_RULES = {
    "map_timeout": ActionRule(
        classification="runtime:map_timeout_after_engine_ready",
        next_actions=(
            "Inspect the latest probe bundle and confirm the last boot phase and missing runtime marker.",
            "Trace the path from engine readiness to `Xash3D GameCube: map loaded <smoke_map>`.",
            "Prefer a narrow fix in game-module registration, map-load, or runtime asset lookup before broader renderer work.",
            "Re-run the bounded Dolphin boot probe after the patch.",
        ),
        focus_files=(
            "engine/platform/gamecube/dll_gamecube.c",
            "engine/server/sv_init.c",
            "engine/common/model.c",
            "engine/common/filesystem_engine.c",
            "engine/platform/gamecube/sys_gamecube.c",
        ),
    ),
    "guest_failure": ActionRule(
        classification="runtime:guest_failure",
        next_actions=(
            "Inspect the guest fatal markers in the latest probe logs.",
            "Find the first subsystem-specific failure after bootstrap and patch only that path.",
            "Re-run the smallest focused verifier or Dolphin probe that reproduces the crash.",
        ),
        focus_files=(
            "engine/common/system.c",
            "engine/platform/gamecube/sys_gamecube.c",
            "engine/platform/gamecube/mem_gamecube.c",
        ),
    ),
    "boot_failure": ActionRule(
        classification="boot:pre_engine_ready_failure",
        next_actions=(
            "Check DOL generation, launcher path, and earliest platform boot markers.",
            "Verify the generated boot artifact and rerun the bounded boot probe.",
        ),
        focus_files=(
            "scripts/elf-to-dol.py",
            "engine/common/launcher.c",
            "engine/platform/gamecube/sys_gamecube.c",
        ),
    ),
    "map_ready": ActionRule(
        classification="runtime:map_ready",
        next_actions=(
            "Extend proof from map ready to gameplay continuity or visual fidelity gates.",
            "Capture screenshot baseline evidence when a matching milestone exists.",
        ),
        focus_files=(
            "scripts/dolphin-boot-probe.sh",
            "scripts/gamecube-screenshot-baseline.py",
            "engine/platform/gamecube/vid_gamecube.c",
        ),
    ),
    "retail_ready": ActionRule(
        classification="ui:retail_menu_ready",
        next_actions=(
            "Capture the menu milestone screenshot and compare it to the retail baseline.",
            "Advance to menu interaction, HUD, or smoke-map runtime validation.",
        ),
        focus_files=(
            "scripts/capture-gc-main-menu.sh",
            "scripts/gamecube-screenshot-baseline.py",
            "engine/client/cl_gameui.c",
        ),
    ),
}


def clip(text: str, limit: int = 280) -> str:
    text = re.sub(r"\s+", " ", text).strip()
    return text if len(text) <= limit else text[: limit - 3].rstrip() + "..."


def read_json(path: Path) -> dict:
    if not path.is_file():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except json.JSONDecodeError:
        return {}


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def latest_probe_result(repo: Path) -> dict:
    memory = read_json(repo / ".ai/state/dolphin-harness-memory.json")
    runs = memory.get("runs", [])
    if isinstance(runs, list) and runs:
        latest = runs[0]
        if isinstance(latest, dict):
            return latest
    return {}


def latest_screenshot_result(repo: Path) -> dict:
    comparisons = repo / ".ai/screenshots/comparisons"
    if not comparisons.is_dir():
        return {}
    candidates = sorted(comparisons.glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    for candidate in candidates:
        data = read_json(candidate)
        if data:
            return data
    return {}


def build_incident(repo: Path) -> dict:
    probe = latest_probe_result(repo)
    screenshot = latest_screenshot_result(repo)
    latest_md = read_text(repo / ".ai/state/dolphin-harness-latest.md")

    classification = "unknown"
    probe_status = ""
    g36_status = ""
    visual = ""
    log_dir = ""
    next_action = ""
    analysis = ""
    timestamp = ""

    if probe:
        cls = probe.get("classification", {})
        if isinstance(cls, dict):
            probe_status = str(cls.get("status", ""))
            g36_status = str(cls.get("g36_status", ""))
            visual = str(cls.get("visual", ""))
        log_dir = str(probe.get("log_dir", ""))
        next_action = str(probe.get("next_action", ""))
        analysis = clip(str(probe.get("analysis", "")), 500)
        timestamp = str(probe.get("timestamp", ""))

    rule = ACTION_RULES.get(probe_status)
    if rule:
        classification = rule.classification
        next_actions = list(rule.next_actions)
        focus_files = list(rule.focus_files)
    else:
        next_actions = ["Inspect the latest structured runtime evidence before editing source."]
        focus_files = []

    screenshot_summary = {}
    if screenshot:
        screenshot_summary = {
            "milestone": screenshot.get("milestone", ""),
            "verdict": screenshot.get("verdict", ""),
            "comparison": screenshot.get("comparison", ""),
            "candidate": screenshot.get("candidate", ""),
            "reference": screenshot.get("reference", ""),
            "metrics": screenshot.get("metrics", {}),
        }
        if screenshot_summary.get("verdict") in {"drift", "mismatch"}:
            next_actions.append(
                f"Use screenshot baseline `{screenshot_summary.get('milestone')}` as a regression gate while fixing the runtime-visible issue."
            )

    return {
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "classification": classification,
        "probe_status": probe_status,
        "g36_status": g36_status,
        "visual_status": visual,
        "latest_probe_log_dir": log_dir,
        "latest_probe_timestamp": timestamp,
        "latest_probe_analysis": analysis,
        "suggested_next_action": next_action,
        "next_actions": next_actions,
        "focus_files": focus_files,
        "latest_screenshot": screenshot_summary,
        "harness_latest_excerpt": clip(latest_md, 900),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(".ai/state/gamecube-harness-incident.json"),
    )
    args = parser.parse_args()

    repo = args.repo.resolve()
    incident = build_incident(repo)
    output = (repo / args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(incident, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(incident, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
