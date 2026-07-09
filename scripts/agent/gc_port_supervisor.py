#!/usr/bin/env python3
import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from gc_common import REPO, repo_path_pattern
STATE = REPO / ".ai/gc-port-supervisor.json"
LOG_DIR = REPO / ".ai/logs/supervisor"
TASK_OUT = REPO / ".ai/next-patch-task.txt"

PHASES = [
    {
        "name": "build_engine",
        "cmd": ["scripts/build-gamecube.sh"],
        "timeout": 900,
        "success": ["GameCube build installed to OUT/", "'build' finished successfully"],
    },
    {
        "name": "build_disc",
        "cmd": ["python3", "scripts/build-gamecube-disc.py", "--output", "OUT/xash3d-gc.iso"],
        "timeout": 300,
        "success": ["Built OUT/xash3d-gc.iso"],
    },
    {
        "name": "dolphin_boot",
        "cmd": ["scripts/dolphin-boot-probe.sh", "OUT/xash3d-gc.iso"],
        "timeout": 240,
        "success": ["MAP_READY:", "G36_STATUS: PASS", "G45_STATUS: PASS", "VISUAL_STATUS: nonblack"],
    },
    {
        "name": "map_compat_probe",
        "cmd": ["scripts/gamecube-map-compat-probe.sh", "c0a0e", "c1a0", "c1a0d", "c2a1"],
        "timeout": 900,
        "success": ["MAP_COMPAT_PROBE: PASS", "MAP_COMPAT_PROBE: PARTIAL"],
    },
]

IGNORE_PATCH_TARGETS = {
    "public/miniz.c",
}

BLOCKED_DEFAULT_TARGETS = {
    "engine/platform/gamecube/vid_gamecube.c",
}

SOURCE_EXTS = {".c", ".cpp", ".cc", ".h", ".hpp", ".hh", ".py", ".sh"}


def run(cmd, timeout, phase):
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    log_path = LOG_DIR / f"{phase}.log"

    print(f"\n== PHASE {phase} ==")
    print("+", " ".join(cmd), flush=True)

    proc = subprocess.Popen(
        cmd,
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )

    output = []
    start = time.time()

    try:
        while True:
            if proc.stdout is not None:
                line = proc.stdout.readline()
                if line:
                    output.append(line)
                    print(line, end="")

            if proc.poll() is not None:
                if proc.stdout is not None:
                    rest = proc.stdout.read()
                    if rest:
                        output.append(rest)
                        print(rest, end="")
                break

            if time.time() - start > timeout:
                print(f"\nTIMEOUT: {phase} exceeded {timeout}s. Killing process group.")
                kill_process_group(proc)
                break

            time.sleep(0.05)

    finally:
        text = "".join(output)
        log_path.write_text(text, encoding="utf-8")
        kill_dolphin_stragglers()

    return proc.returncode if proc.returncode is not None else 124, text, str(log_path.relative_to(REPO))


def kill_process_group(proc, grace=5):
    try:
        pgid = os.getpgid(proc.pid)
    except ProcessLookupError:
        return

    try:
        os.killpg(pgid, signal.SIGTERM)
    except ProcessLookupError:
        return

    deadline = time.time() + grace
    while time.time() < deadline:
        if proc.poll() is not None:
            return
        time.sleep(0.25)

    try:
        os.killpg(pgid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def kill_dolphin_stragglers():
    try:
        ps = subprocess.run(
            ["ps", "-eo", "pid=,comm=,args="],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        return

    repo_markers = [
        str(REPO),
        "xash3d-gc.iso",
        ".ai/dolphin-user-run",
        ".ai/logs/dolphin-probe",
    ]

    emulator_markers = [
        "dolphin-emu",
        "dolphin-emu-nogui",
        "DolphinQt",
        "org.DolphinEmu.dolphin-emu",
    ]

    skip_markers = [
        "dolphin-boot-probe.sh",
        "gc_port_supervisor.py",
        "python3 scripts/agent",
        "/bin/bash",
    ]

    victims = []

    for line in ps.stdout.splitlines():
        parts = line.strip().split(maxsplit=2)
        if len(parts) < 3:
            continue

        pid_s, comm, args = parts
        try:
            pid = int(pid_s)
        except ValueError:
            continue

        haystack = f"{comm} {args}"

        if pid == os.getpid():
            continue
        if any(skip in haystack for skip in skip_markers):
            continue
        if not any(marker in haystack for marker in emulator_markers):
            continue
        if not any(marker in haystack for marker in repo_markers):
            continue

        victims.append(pid)

    for pid in victims:
        try:
            print(f"Killing Dolphin emulator pid={pid}")
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass

    time.sleep(2)

    for pid in victims:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            continue
        try:
            print(f"Force-killing Dolphin emulator pid={pid}")
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def success_for_phase(phase, code, log):
    # Dolphin scripts may return nonzero if killed after useful evidence,
    # so success markers are authoritative.
    if phase["name"] in {"dolphin_boot", "map_compat_probe"}:
        return any(marker in log for marker in phase["success"])

    if code != 0:
        return False

    return any(marker in log for marker in phase["success"])


def classify_failure(log):
    low = log.lower()

    if "mem fail" in low or "_mem_alloc: out of memory" in low or "xash3d gamecube: fatal message=" in low:
        return "memory"
    if "guest_failure" in low or "map_loaded_no_input" in low:
        return "runtime_probe"
    if "undefined reference" in low or "collect2: error" in low or "ld returned" in low:
        return "linker"
    if re.search(r"^\.\./[^:\n]+:\d+(?::\d+)?:\s+fatal error:", log, re.M):
        return "missing_header"
    if re.search(r"^\.\./[^:\n]+:\d+(?::\d+)?:\s+error:", log, re.M):
        return "compile"
    if "traceback" in low:
        return "script_exception"
    if "timeout:" in low:
        return "timeout"
    return "runtime_or_unknown"


def extract_patch_targets(log, failure_kind: str | None = None):
    repo_pat = repo_path_pattern()
    patterns = [
        r"^\.\./([^:\n]+):\d+(?::\d+)?:\s+(?:fatal error|error):",
        rf"^({repo_pat}/[^:\n]+):\d+(?::\d+)?:\s+(?:fatal error|error):",
        rf'File "({repo_pat}/[^"\n]+)", line \d+',
        r"(?:at=| at )\.?\.?/?(engine/[^:\s\n]+):\d+",
        r"(?:at=| at )\.?\.?/?(ref/[^:\s\n]+):\d+",
    ]

    found = []
    for line in log.splitlines():
        for pat in patterns:
            m = re.search(pat, line)
            if not m:
                continue

            target = m.group(1)
            if target.startswith(str(REPO)):
                target = str(Path(target).resolve().relative_to(REPO))
            target = target.lstrip("./")
            if target.startswith("../"):
                target = target[3:]

            if target in IGNORE_PATCH_TARGETS:
                continue
            if target in BLOCKED_DEFAULT_TARGETS:
                continue
            if Path(target).suffix in SOURCE_EXTS:
                found.append(target)

    if failure_kind == "memory" and not found:
        found.extend(
            [
                "engine/common/mod_bmodel.c",
                "engine/platform/gamecube/mem_gamecube.c",
                "engine/common/zone.c",
            ]
        )

    seen = set()
    out = []
    for item in found:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out[:3]


def write_patch_task(report):
    targets = report.get("patch_targets", [])
    TASK_OUT.parent.mkdir(parents=True, exist_ok=True)
    TASK_OUT.write_text(
        "Auto-port task for Xash3D GameCube\n"
        "===================================\n\n"
        f"Failed phase: {report.get('failed_phase')}\n"
        f"Failure kind: {report.get('failure_kind')}\n"
        f"Patch targets: {targets}\n"
        f"Log path: {report.get('log_path')}\n\n"
        "Rules:\n"
        "- Patch only the first target unless the error requires a header/source pair.\n"
        "- Do not touch generated build/ files.\n"
        "- Do not touch engine/platform/gamecube/vid_gamecube.c unless the error names it.\n"
        "- Ignore public/miniz.c pragma notes.\n"
        "- Keep the patch small and compile/probe-driven.\n\n"
        "Error context:\n"
        "--------------\n"
        f"{report.get('error_context', '')}\n",
        encoding="utf-8",
    )


def first_error_context(log):
    lines = log.splitlines()

    for i, line in enumerate(lines):
        if re.search(r"^\.\./[^:\n]+:\d+(?::\d+)?:\s+(fatal error|error):", line):
            return "\n".join(lines[max(0, i - 8):min(len(lines), i + 36)])

    for i, line in enumerate(lines):
        if "undefined reference" in line or "Traceback" in line or "ERROR:" in line:
            return "\n".join(lines[max(0, i - 12):min(len(lines), i + 40)])

    return "\n".join(lines[-120:])


def save_state(report):
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"\nWrote {STATE.relative_to(REPO)}")
    print(json.dumps(report, indent=2)[:6000])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stop-after", choices=["build_engine", "build_disc", "dolphin_boot"])
    args = ap.parse_args()

    full_log = ""

    for phase in PHASES:
        code, log, log_path = run(phase["cmd"], phase["timeout"], phase["name"])
        full_log += f"\n\n===== {phase['name']} =====\n{log}"

        ok = success_for_phase(phase, code, log)

        if not ok:
            context = first_error_context(log)
            failure_kind = classify_failure(log)
            patch_targets = extract_patch_targets(log, failure_kind)
            report = {
                "ok": False,
                "failed_phase": phase["name"],
                "exit_code": code,
                "failure_kind": failure_kind,
                "patch_targets": patch_targets,
                "error_context": context,
                "log_path": log_path,
                "next_action": "patch_first_failure_file" if patch_targets else "inspect_runtime_evidence",
            }
            write_patch_task(report)
            save_state(report)
            return 1

        if args.stop_after == phase["name"]:
            report = {
                "ok": True,
                "stopped_after": phase["name"],
                "log_path": log_path,
                "next_action": "continue_next_phase",
            }
            save_state(report)
            return 0

    report = {
        "ok": True,
        "failed_phase": None,
        "failure_kind": None,
        "patch_targets": [],
        "next_action": "hardware_handoff_or_expand_tests",
        "artifacts": [
            "OUT/bin/boot.dol",
            "OUT/xash3d-gc.iso",
        ],
    }
    save_state(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
