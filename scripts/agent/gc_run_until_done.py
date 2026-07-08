#!/usr/bin/env python3
"""Run the GameCube port supervisor loop until the pipeline passes or a blocker stops automation."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from itertools import count
from pathlib import Path

from gc_autopilot import apply_known_fix
from gc_common import (
    REPO,
    SupervisorLock,
    bootstrap_env,
    commit_changes,
    git_dirty_source_paths,
    model_ready,
    run,
)

STATE_PATH = REPO / ".ai/gc-port-supervisor.json"
TASK_PATH = REPO / ".ai/next-patch-task.txt"
TASK_FILE = REPO / ".ai/tasks/gc-port-current.md"

RECOVERABLE_AIDER_STATUSES = {10, 15, 16, 17, 18, 19}
FAST_RETRY_STATUSES = {1, 3, 10, 15, 16, 17, 18, 19}


def load_supervisor_report() -> dict | None:
    if not STATE_PATH.is_file():
        return None
    return json.loads(STATE_PATH.read_text(encoding="utf-8"))


def supervisor_report_for_fixes(report: dict) -> dict:
    return {
        "build_ok": bool(report.get("ok")),
        "failure_kind": report.get("failure_kind"),
        "patch_targets": report.get("patch_targets", []),
        "error_context": report.get("error_context", ""),
        "build_reason": report.get("failed_phase"),
    }


def commit_subject(report: dict) -> str:
    failure = report.get("failure_kind") or "build"
    targets = report.get("patch_targets") or []
    target = Path(targets[0]).name if targets else "gamecube"
    phase = report.get("failed_phase") or report.get("build_reason") or "pipeline"
    return f"fix: resolve GameCube {failure} in {target} ({phase})"


def write_aider_task(report: dict) -> Path:
    TASK_FILE.parent.mkdir(parents=True, exist_ok=True)
    targets = report.get("patch_targets") or []
    primary = targets[0] if targets else "(none named)"

    if TASK_PATH.is_file():
        body = TASK_PATH.read_text(encoding="utf-8")
    else:
        body = (
            "Auto-port task for Xash3D GameCube\n"
            "===================================\n\n"
            f"Failed phase: {report.get('failed_phase')}\n"
            f"Failure kind: {report.get('failure_kind')}\n"
            f"Patch target: {primary}\n\n"
            f"{report.get('error_context', '')}\n"
        )

    TASK_FILE.write_text(
        body
        + "\n\nAutomation pass rules:\n"
        "- Patch only the first named target unless a header/source pair is required.\n"
        "- Do not touch generated build/ files.\n"
        "- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.\n"
        "- Ignore public/miniz.c pragma notes.\n"
        "- Keep the patch small and compile/probe-driven.\n"
        "- There is no interactive human; do not ask questions.\n",
        encoding="utf-8",
    )
    return TASK_FILE


def run_supervisor(stop_after: str | None) -> tuple[int, dict | None]:
    cmd = ["python3", "scripts/agent/gc_port_supervisor.py"]
    if stop_after:
        cmd.extend(["--stop-after", stop_after])
    code, _ = run(cmd)
    report = load_supervisor_report()
    return code, report


def run_aider_pass(report: dict) -> int:
    targets = report.get("patch_targets") or []
    if not targets:
        print("gc-run-until-done: supervisor named no patch targets; stopping.", file=sys.stderr)
        return 4

    primary = targets[0]
    if not (REPO / primary).is_file():
        print(f"gc-run-until-done: patch target missing on disk: {primary}", file=sys.stderr)
        return 4

    task_path = write_aider_task(report)
    env = os.environ.copy()
    env["AI_COMMIT_SUBJECT"] = commit_subject(report)
    env["AI_COMMIT_BODY"] = (
        "Automated GameCube port patch from gc_port_supervisor evidence.\n"
        f"Failed phase: {report.get('failed_phase')}\n"
        f"Failure kind: {report.get('failure_kind')}\n"
        f"Primary target: {primary}\n"
    )
    env.setdefault("AIDER_AUTOMATION", "1")
    env.setdefault("AI_VERIFY_REQUIRE_DOC_UPDATE", "0")
    env.setdefault("AI_ENFORCE_EDITABLE_CONTEXT", "1")

    cmd = [
        "scripts/ai-aider-pass.sh",
        str(REPO),
        str(task_path),
        primary,
        "read:.ai/prompts/GAMECUBE_LOCAL_MISSION.md",
        "read:.ai/prompts/GAMECUBE_FAILURE_MEMORY.md",
    ]

    print("+", " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=REPO, env=env, check=False)
    return proc.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max-cycles", type=int, default=0, help="0 means unlimited")
    parser.add_argument("--sleep", type=int, default=20, help="seconds between recoverable failures")
    parser.add_argument(
        "--stop-after",
        choices=["build_engine", "build_disc", "dolphin_boot"],
        help="forward to gc_port_supervisor for single-phase smoke runs",
    )
    parser.add_argument(
        "--probe-only",
        action="store_true",
        help="run one supervisor pass and exit without patching",
    )
    args = parser.parse_args()

    bootstrap_env()

    lock = SupervisorLock()
    if not lock.acquire():
        print("gc-run-until-done: another port automation loop is already running", file=sys.stderr)
        return 2

    try:
        api_base = os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1")
        cycles = count(1) if args.max_cycles == 0 else range(1, args.max_cycles + 1)

        for cycle in cycles:
            dirty = git_dirty_source_paths()
            if dirty and not args.probe_only:
                print(
                    "gc-run-until-done: source tree already has edits; refusing to stack automated patches:",
                    ", ".join(dirty[:5]),
                    file=sys.stderr,
                )
                return 2

            if not args.probe_only and not model_ready(api_base):
                print(
                    f"gc-run-until-done: model API is not reachable at {api_base}; retrying after {args.sleep}s",
                    file=sys.stderr,
                )
                time.sleep(args.sleep)
                continue

            limit = "unlimited" if args.max_cycles == 0 else str(args.max_cycles)
            print(f"\n== gc port automation cycle {cycle}/{limit} ==", flush=True)

            code, report = run_supervisor(args.stop_after)
            if report is None:
                print("gc-run-until-done: supervisor produced no state report", file=sys.stderr)
                return 1

            if report.get("ok"):
                print("gc-run-until-done: GameCube pipeline passed all supervisor phases.")
                return 0

            if args.probe_only:
                print("gc-run-until-done: probe-only mode; supervisor reported a failure.")
                return code or 1

            fix_report = supervisor_report_for_fixes(report)
            ok, message = apply_known_fix(fix_report)
            if ok:
                run(["git", "diff", "--"] + (fix_report.get("patch_targets") or []))
                if commit_changes(message):
                    continue
                print("gc-run-until-done: known fix produced no commit; continuing.", file=sys.stderr)

            aider_status = run_aider_pass(report)
            if aider_status == 0:
                continue
            if aider_status in FAST_RETRY_STATUSES:
                print(
                    f"gc-run-until-done: recoverable child exit {aider_status}; retrying after {args.sleep}s",
                    file=sys.stderr,
                )
                time.sleep(args.sleep)
                continue

            print(f"gc-run-until-done: non-recoverable child exit {aider_status}", file=sys.stderr)
            return aider_status

        if args.max_cycles > 0:
            print("gc-run-until-done: cycle limit reached before pipeline passed", file=sys.stderr)
            return 3
        return 0
    finally:
        lock.release()


if __name__ == "__main__":
    raise SystemExit(main())
