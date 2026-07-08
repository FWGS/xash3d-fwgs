#!/usr/bin/env python3
import json
import re
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from gc_common import REPO, normalize_repo_path, repo_path_pattern
AGENDA_PATH = REPO / ".ai/gc-reagent-agenda.json"
REPORT_PATH = REPO / ".ai/reagent-last-probe.json"
LAST_LOG_PATH = REPO / ".ai/logs/reagent-last-build.log"

DEFAULT_BLOCKED_PATHS = {
    "engine/platform/gamecube/vid_gamecube.c",
}

DEFAULT_FALLBACK_QUEUE = [
    "scripts",
    "engine/client",
    "engine/common",
    "engine/filesystem",
    "engine/platform/gamecube",
    "engine/server",
    "ref/gx",
    "3rdparty/hlsdk-portable",
]

SOURCE_EXTS = {".c", ".cpp", ".cc", ".h", ".hpp", ".hh", ".cmake", ".py", ".sh"}
SOURCE_FILENAMES = {"CMakeLists.txt", "wscript", "wscript_build"}

WATCHED_SOURCE_ROOTS = [
    "scripts",
    "engine",
    "ref",
    "stub",
    "public",
    "3rdparty/hlsdk-portable",
]


def run(cmd, *, cwd=REPO):
    print("+", " ".join(cmd), flush=True)
    p = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    print(p.stdout)
    return p.returncode, p.stdout


def load_agenda():
    if AGENDA_PATH.exists():
        return json.loads(AGENDA_PATH.read_text(encoding="utf-8"))

    return {
        "blocked_default_paths": sorted(DEFAULT_BLOCKED_PATHS),
        "fallback_subsystem_queue": DEFAULT_FALLBACK_QUEUE,
        "phase_order": [
            "gamecube_engine_build",
            "gamecube_disc_build",
            "dolphin_boot_probe",
            "gamecube_rc_gate",
        ],
        "notes": [
            "Do not default to vid_gamecube.c.",
            "Only patch files named by concrete build/probe failures.",
            "Prefer scripts/build-gamecube.sh over host CMake builds.",
        ],
    }


def is_source_path(path):
    p = Path(path)
    return p.suffix in SOURCE_EXTS or p.name in SOURCE_FILENAMES


def git_dirty_source_state():
    code, out = run(["git", "status", "--short"])

    dirty_source = []
    for line in out.splitlines():
        path = line[3:] if len(line) > 3 else line
        path = path.strip()

        if " -> " in path:
            path = path.split(" -> ", 1)[1].strip()

        if path.startswith(".ai/reagent-last-probe.json"):
            continue
        if path.startswith(".ai/logs/"):
            continue
        if path == "nohup.out":
            continue

        if any(path.startswith(root + "/") or path == root for root in WATCHED_SOURCE_ROOTS):
            if is_source_path(path):
                dirty_source.append(line)

    return dirty_source, out


def phase_commands():
    return [
        (["scripts/build-gamecube.sh"], "gamecube_engine_build"),
        (["python3", "scripts/build-gamecube-disc.py", "--output", "OUT/xash3d-gc.iso"], "gamecube_disc_build"),
        (["scripts/dolphin-boot-probe.sh", "OUT/xash3d-gc.iso"], "dolphin_boot_probe"),
    ]


def phase_entrypoint_exists(cmd):
    if not cmd:
        return False

    # Commands launched through Python should validate the script path, not
    # REPO/python3.
    if cmd[0] == "python3" and len(cmd) > 1:
        return (REPO / cmd[1]).exists()

    # Repo-local script path.
    if "/" in cmd[0]:
        return (REPO / cmd[0]).exists()

    # System command such as python3, timeout, bash, etc.
    return True


def available_phase_commands():
    phases = []
    for cmd, reason in phase_commands():
        if phase_entrypoint_exists(cmd):
            phases.append((cmd, reason))
    return phases


def compiler_error_lines(log):
    lines = log.splitlines()

    # Match real compiler diagnostics only. Do not match Waf configure text like
    # "-Werror=implicit-function-declaration".
    diagnostic = re.compile(
        rf"^(?:\.\./|{repo_path_pattern()}/)"
        r"[^:\n]+:\d+(?::\d+)?:\s+"
        r"(?:fatal error|error|warning|note):"
    )

    hits = []
    for i, line in enumerate(lines):
        if diagnostic.search(line):
            hits.append(i)

    return hits


def extract_error_context(log):
    lines = log.splitlines()
    hits = compiler_error_lines(log)

    if hits:
        i = hits[0]
        start = max(0, i - 8)
        end = min(len(lines), i + 36)
        return "\n".join(lines[start:end])

    linker_patterns = [
        re.compile(r"undefined reference"),
        re.compile(r"collect2: error"),
        re.compile(r"ld returned"),
    ]

    for i, line in enumerate(lines):
        if any(rx.search(line) for rx in linker_patterns):
            start = max(0, i - 16)
            end = min(len(lines), i + 40)
            return "\n".join(lines[start:end])

    script_patterns = [
        re.compile(r"Traceback"),
        re.compile(r"Exception:"),
        re.compile(r"^ERROR:"),
    ]

    for i, line in enumerate(lines):
        if any(rx.search(line) for rx in script_patterns):
            start = max(0, i - 12)
            end = min(len(lines), i + 40)
            return "\n".join(lines[start:end])

    # Last useful fallback: show the end where Waf reports the failed task.
    return "\n".join(lines[-180:])


def classify_failure(error_context):
    low = error_context.lower()

    if "undefined reference" in low or "ld returned" in low or "collect2: error" in low:
        return "linker"
    if "fatal error:" in low and "no such file or directory" in low:
        return "missing_header"
    if ": error:" in low or "fatal error:" in low:
        return "compile"
    if "traceback" in low or "exception:" in low:
        return "script_exception"
    if "build failed" in low:
        return "build_system"
    if ": warning:" in low:
        return "warning_only"
    return "runtime_or_unknown"


def extract_failure_files(log):
    files = []

    patterns = [
        # ../engine/client/cl_scrn.c:1025:13: error:
        r"^\.\./([^:\n]+):\d+:\d+:\s+(?:fatal error|error|warning|note):",

        # ../engine/client/cl_scrn.c:1014: error:
        r"^\.\./([^:\n]+):\d+:\s+(?:fatal error|error|warning|note):",

        # absolute repo path:line:col: error:
        rf"^({repo_path_pattern()}/[^:\n]+):\d+:\d+:\s+(?:fatal error|error|warning|note):",

        # absolute repo path:line: error:
        rf"^({repo_path_pattern()}/[^:\n]+):\d+:\s+(?:fatal error|error|warning|note):",

        # Python tracebacks.
        rf'File "({repo_path_pattern()}/[^"\n]+)", line \d+',
    ]

    for line in log.splitlines():
        for pat in patterns:
            m = re.search(pat, line)
            if not m:
                continue

            candidate = normalize_repo_path(m.group(1))

            if candidate.startswith("build/"):
                continue
            if candidate.startswith(".ai/"):
                continue

            if is_source_path(candidate):
                files.append(candidate)

    seen = set()
    ordered = []
    for f in files:
        if f not in seen:
            seen.add(f)
            ordered.append(f)

    return ordered


def choose_patch_targets(error_context, full_log, agenda):
    blocked = set(agenda.get("blocked_default_paths", []))

    failure_files = extract_failure_files(error_context)
    if not failure_files:
        failure_files = extract_failure_files(full_log)

    selected = []
    for f in failure_files:
        if f in blocked:
            continue
        if is_source_path(f):
            selected.append(f)

    if selected:
        return selected[:3], "failure_files"

    fallback = []
    for root in agenda.get("fallback_subsystem_queue", DEFAULT_FALLBACK_QUEUE):
        p = REPO / root

        if p.is_file():
            rel = str(p.relative_to(REPO))
            if rel not in blocked and is_source_path(rel):
                fallback.append(rel)

        elif p.is_dir():
            for child in sorted(p.rglob("*")):
                if not child.is_file():
                    continue
                rel = str(child.relative_to(REPO))
                if rel in blocked:
                    continue
                if rel.startswith("build/") or rel.startswith(".ai/"):
                    continue
                if is_source_path(rel):
                    fallback.append(rel)
                    break

    return fallback[:3], "agenda_fallback"


def write_report(report):
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Wrote {REPORT_PATH}")
    print(json.dumps(report, indent=2)[:8000])


def main():
    print("== GC reagent ==")

    agenda = load_agenda()

    dirty_source, status = git_dirty_source_state()
    if dirty_source:
        report = {
            "build_ok": False,
            "failure_kind": "dirty_source_tree",
            "message": "Source tree already has edits; refusing to stack automated patches.",
            "dirty_source": dirty_source,
            "git_status": status,
            "agenda": agenda,
        }
        write_report(report)
        return 2

    phases = available_phase_commands()
    if not phases:
        report = {
            "build_ok": False,
            "failure_kind": "no_gamecube_build_surface",
            "message": "No GameCube build/probe scripts found. Expected scripts/build-gamecube.sh.",
            "git_status": status,
            "agenda": agenda,
        }
        write_report(report)
        return 1

    combined_log = ""
    last_cmd = None
    last_reason = None
    last_code = 0

    for cmd, reason in phases:
        last_cmd = cmd
        last_reason = reason
        code, log = run(cmd)
        last_code = code
        combined_log += f"\n\n===== PHASE {reason}: {' '.join(cmd)} =====\n"
        combined_log += log

        if code != 0:
            break

    LAST_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    LAST_LOG_PATH.write_text(combined_log, encoding="utf-8")

    error_context = extract_error_context(combined_log)
    failure = classify_failure(error_context)
    patch_targets, target_reason = choose_patch_targets(error_context, combined_log, agenda)

    report = {
        "build_ok": last_code == 0,
        "failure_kind": failure,
        "build_reason": last_reason,
        "build_cmd": last_cmd,
        "patch_targets": patch_targets,
        "patch_target_reason": target_reason,
        "error_context": error_context,
        "log_path": str(LAST_LOG_PATH.relative_to(REPO)),
        "git_status": status,
        "agenda": agenda,
        "next_action": (
            "patch_first_failure_file"
            if patch_targets
            else "inspect_log_manually"
        ),
    }

    write_report(report)
    return 0 if last_code == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
