#!/usr/bin/env python3
"""Maintain a compact recursive goal ledger for the GameCube port harness."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def read_json(path: Path) -> dict:
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except json.JSONDecodeError:
        return {}
    return data if isinstance(data, dict) else {}


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def child_id_for(action: str) -> str:
    digest = hashlib.sha1(action.encode("utf-8")).hexdigest()[:8]
    return f"RG-{digest}"


def classify_stage(action: str) -> str:
    text = action.lower()
    if "inspect" in text or "confirm" in text:
        return "inspect"
    if "trace" in text or "find" in text:
        return "trace"
    if "fix" in text or "patch" in text or "prefer a narrow fix" in text:
        return "patch"
    if "re-run" in text or "rerun" in text or "verify" in text:
        return "verify"
    if "screenshot" in text or "baseline" in text:
        return "regress"
    return "general"


def choose_active(children: list[dict]) -> str | None:
    open_children = [c for c in children if c.get("status") not in {"done", "blocked"}]
    if not open_children:
        return None

    def key(child: dict) -> tuple[int, int, str]:
        status_rank = 0 if child.get("status") == "pending" else 1
        priority = int(child.get("priority", 999) or 999)
        attempts = int(child.get("attempts", 0) or 0)
        return (status_rank, priority, attempts, str(child.get("id", "")))

    return min(open_children, key=key).get("id")


def render_task_summary(state: dict) -> str:
    lines = [
        "# GameCube Recursive Task",
        "",
        f"- Updated: {state.get('generated_at', '')}",
        f"- Root: {state.get('root_goal', {}).get('title', 'unknown')}",
        f"- Active child: {state.get('active_child_title', 'none')}",
        f"- Classification: {state.get('classification', 'unknown')}",
    ]
    active = None
    for child in state.get("children", []):
        if child.get("id") == state.get("active_child_id"):
            active = child
            break
    if isinstance(active, dict):
        lines.append(f"- Stage: {active.get('stage', 'general')}")
        lines.append(f"- Action: {active.get('action', '')}")
        focus_files = active.get("focus_files", [])
        if isinstance(focus_files, list) and focus_files:
            lines.append("- Focus files: " + ", ".join(str(item) for item in focus_files[:5]))
    lines += ["", "## Child queue"]
    for child in state.get("children", [])[:8]:
        lines.append(
            f"- {child.get('id')}: {child.get('status')} attempts={child.get('attempts', 0)} {child.get('action', '')}"
        )
    return "\n".join(lines).rstrip() + "\n"


def refresh_state(existing: dict, incident: dict) -> dict:
    classification = str(incident.get("classification", "unknown"))
    probe_status = str(incident.get("probe_status", ""))
    focus_files = incident.get("focus_files", [])
    if not isinstance(focus_files, list):
        focus_files = []
    next_actions = incident.get("next_actions", [])
    if not isinstance(next_actions, list):
        next_actions = []

    existing_children = {}
    for child in existing.get("children", []):
        if isinstance(child, dict) and child.get("id"):
            existing_children[str(child["id"])] = child

    children = []
    for index, action in enumerate(next_actions, start=1):
        action = str(action).strip()
        if not action:
            continue
        child_id = child_id_for(action)
        prior = dict(existing_children.get(child_id, {}))
        child = {
            "id": child_id,
            "title": f"{classification} child {index}",
            "action": action,
            "stage": classify_stage(action),
            "priority": index,
            "status": str(prior.get("status", "pending")),
            "attempts": int(prior.get("attempts", 0) or 0),
            "focus_files": list(prior.get("focus_files", focus_files[:5])),
            "last_result": str(prior.get("last_result", "")),
            "last_log": str(prior.get("last_log", "")),
            "last_selected_at": str(prior.get("last_selected_at", "")),
        }
        if child["status"] not in {"pending", "in_progress", "attempted", "done", "blocked"}:
            child["status"] = "pending"
        children.append(child)

    existing_active_child_id = str(existing.get("active_child_id", "") or "")
    if existing_active_child_id:
        for child in children:
            if child["id"] == existing_active_child_id and child["status"] in {"in_progress", "pending", "attempted"}:
                active_child_id = existing_active_child_id
                break
        else:
            active_child_id = choose_active(children)
    else:
        active_child_id = choose_active(children)
    active_child_title = ""
    for child in children:
        if child["id"] == active_child_id:
            child["status"] = "in_progress"
            child["last_selected_at"] = now_iso()
            active_child_title = child["action"]
        elif child["status"] == "in_progress":
            child["status"] = "attempted"

    return {
        "generated_at": now_iso(),
        "classification": classification,
        "probe_status": probe_status,
        "root_goal": {
            "id": f"ROOT-{probe_status or 'unknown'}",
            "title": f"Resolve {classification}",
            "status": "in_progress" if children else "pending",
        },
        "active_child_id": active_child_id,
        "active_child_title": active_child_title,
        "children": children,
        "source": {
            "incident_generated_at": incident.get("generated_at", ""),
            "latest_probe_log_dir": incident.get("latest_probe_log_dir", ""),
            "latest_screenshot": incident.get("latest_screenshot", {}),
        },
        "history": list(existing.get("history", []))[-24:],
    }


def finalize_state(existing: dict, result: str, exit_status: int, log_path: str, repo_changed: bool) -> dict:
    active_id = existing.get("active_child_id")
    history = list(existing.get("history", []))[-23:]
    active_title = existing.get("active_child_title", "")

    for child in existing.get("children", []):
        if child.get("id") != active_id:
            continue
        child["attempts"] = int(child.get("attempts", 0) or 0) + 1
        child["last_result"] = result
        child["last_exit_status"] = exit_status
        child["last_log"] = log_path
        child["last_finished_at"] = now_iso()
        if result == "COMPLETE":
            child["status"] = "done"
        elif result == "BLOCKED":
            child["status"] = "blocked"
        else:
            child["status"] = "attempted" if repo_changed else "pending"
        history.append(
            {
                "child_id": child.get("id"),
                "title": child.get("title", ""),
                "action": child.get("action", ""),
                "result": result,
                "repo_changed": repo_changed,
                "exit_status": exit_status,
                "log": log_path,
                "finished_at": child["last_finished_at"],
            }
        )
        break

    existing["history"] = history[-24:]
    existing["generated_at"] = now_iso()
    return existing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--incident", type=Path, default=Path(".ai/state/gamecube-harness-incident.json"))
    parser.add_argument("--output", type=Path, default=Path(".ai/state/gamecube-recursive-goals.json"))
    parser.add_argument("--task-output", type=Path, default=Path(".continue/gamecube-agent/recursive-task.md"))
    parser.add_argument("--finalize", action="store_true")
    parser.add_argument("--result", default="")
    parser.add_argument("--exit-status", type=int, default=0)
    parser.add_argument("--log", default="")
    parser.add_argument("--repo-changed", action="store_true")
    args = parser.parse_args()

    repo = args.repo.resolve()
    output = (repo / args.output).resolve()
    task_output = (repo / args.task_output).resolve()
    existing = read_json(output)

    if args.finalize:
        state = finalize_state(existing, args.result, args.exit_status, args.log, args.repo_changed)
    else:
        incident = read_json((repo / args.incident).resolve())
        state = refresh_state(existing, incident)

    write_json(output, state)
    task_output.parent.mkdir(parents=True, exist_ok=True)
    task_output.write_text(render_task_summary(state), encoding="utf-8")
    print(json.dumps(state, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
