#!/usr/bin/env python3
import json
import re
import subprocess
from pathlib import Path

REPO = Path("/home/tim/Desktop/xash3d-gc")
AGENDA_PATH = REPO / ".ai/gc-reagent-agenda.json"
REPORT_PATH = REPO / ".ai/reagent-last-probe.json"

DEFAULT_BLOCKED_PATHS = {
    "engine/platform/gamecube/vid_gamecube.c",
}

DEFAULT_FALLBACK_QUEUE = [
    "scripts",
    "engine/platform/gamecube",
    "engine/common",
    "engine/filesystem",
    "engine/client",
    "engine/server",
    "ref/gx",
    "3rdparty/hlsdk-portable",
]

SOURCE_EXTS = {".c", ".cpp", ".h", ".hpp", ".cmake", ".py", ".sh"}
SOURCE_FILENAMES = {"CMakeLists.txt", "wscript", "wscript_build"}

WATCHED_SOURCE_ROOTS = [
    "scripts",
    "engine/platform/gamecube",
    "engine/common",
    "engine/filesystem",
    "engine/client",
    "engine/server",
    "ref/gx",
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
        # status format: XY path
        path = line[3:] if len(line) > 3 else line
        path = path.strip()

        if " -> " in path:
            path = path.split(" -> ", 1)[1].strip()

        if any(path.startswith(root + "/") or path == root for root in WATCHED_SOURCE_ROOTS):
            if is_source_path(path):
                dirty_source.append(line)

    return dirty_source, out


def choose_build_command():
    preferred = [
        (["scripts/build-gamecube.sh"], "gamecube_engine_build"),
        (["python3", "scripts/build-gamecube-disc.py", "--output", "OUT/xash3d-gc.iso"], "gamecube_disc_build"),
        (["scripts/dolphin-boot-probe.sh", "OUT/xash3d-gc.iso"], "dolphin_boot_probe"),
        (["scripts/gamecube-rc-check.sh"], "gamecube_rc_gate"),
    ]

    for cmd, reason in preferred:
        if (REPO / cmd[0]).exists():
            return cmd, reason

    return None, "no_gamecube_build_surface"


def extract_error_context(log):
    lines = log.splitlines()
    patterns = [
        "fatal error:",
        " error:",
        "undefined reference",
        "collect2: error",
        "ld returned",
        "gmake:",
        "make:",
        "No such file or directory",
        "Traceback",
        "Exception",
        "ERROR",
    ]

    for i, line in enumerate(lines):
        low = line.lower()
        if any(p.lower() in low for p in patterns):
            start = max(0, i - 12)
            end = min(len(lines), i + 24)
            return "\n".join(lines[start:end])

    return "\n".join(lines[-100:])


def classify_failure(log):
    low = log.lower()

    if "no_gamecube_build_surface" in low:
        return "no_gamecube_build_surface"
    if "fatal error:" in low and "no such file or directory" in low:
        return "missing_header"
    if "undefined reference" in low or "ld returned" in low or "collect2: error" in low:
        return "linker"
    if "fatal error:" in low or " error:" in low:
        return "compile"
    if "traceback" in low or "exception" in low:
        return "script_exception"
    if "gmake:" in low or "make:" in low:
        return "build_system"
    if "warning:" in low:
        return "warning_only"
    return "runtime_or_unknown"


def normalize_repo_path(path):
    path = path.strip()

    if path.startswith(str(REPO)):
        try:
            return str(Path(path).resolve().relative_to(REPO))
        except ValueError:
            return path

    path = path.lstrip("./")
    return path


def extract_failure_files(log):
    files = []

    patterns = [
        # /home/tim/Desktop/xash3d-gc/foo/bar.c:12:34: error:
        r"(/home/tim/Desktop/xash3d-gc/[^:\n]+):\d+:\d+:\s+(?:fatal error|error|warning):",

        # from /home/tim/Desktop/xash3d-gc/foo/bar.c:12
        r"from (/home/tim/Desktop/xash3d-gc/[^:\n]+):\d+",

        # Python/script tracebacks
        r'File "(/home/tim/Desktop/xash3d-gc/[^"\n]+)", line \d+',

        # CMake/make object paths that preserve source-ish names
        r"Building [A-Z]+ object .*?\.dir/(.+?)\.o",
    ]

    for pat in patterns:
        for m in re.finditer(pat, log):
            candidate = normalize_repo_path(m.group(1))

            # CMake object path can look like foo.cpp, or foo.cpp.1, or __/path/foo.c
            candidate = candidate.replace("__/", "../")
            candidate = re.sub(r"\.\d+$", "", candidate)

            if candidate.startswith("../"):
                candidate = candidate[3:]

            if is_source_path(candidate):
                files.append(candidate)

    # Keep order but dedupe.
    seen = set()
    ordered = []
    for f in files:
        if f not in seen:
            seen.add(f)
            ordered.append(f)

    return ordered


def choose_patch_targets(error_context, agenda):
    blocked = set(agenda.get("blocked_default_paths", []))
    failure_files = extract_failure_files(error_context)

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
                if is_source_path(rel):
                    fallback.append(rel)
                    break

    return fallback[:3], "agenda_fallback"


def write_report(report):
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Wrote {REPORT_PATH}")
    print(json.dumps(report, indent=2)[:6000])


def main():
    print("== GC reagent ==")

    agenda = load_agenda()

    dirty_source, status = git_dirty_source_state()
    if dirty_source:
        report = {
            "build_ok": False,
            "failure_kind": "dirty_source_tree",
            "message": "Source tree already has edits; refusing to stack patches.",
            "dirty_source": dirty_source,
            "git_status": status,
            "agenda": agenda,
        }
        write_report(report)
        return 2

    build_cmd, build_reason = choose_build_command()

    if build_cmd is None:
        report = {
            "build_ok": False,
            "failure_kind": "no_gamecube_build_surface",
            "build_reason": build_reason,
            "message": "No GameCube build script found. Expected scripts/build-gamecube.sh.",
            "git_status": status,
            "agenda": agenda,
        }
        write_report(report)
        return 1

    code, build_log = run(build_cmd)

    error_context = extract_error_context(build_log)
    failure = classify_failure(error_context)
    patch_targets, target_reason = choose_patch_targets(error_context, agenda)

    report = {
        "build_ok": code == 0,
        "failure_kind": failure,
        "build_reason": build_reason,
        "build_cmd": build_cmd,
        "patch_targets": patch_targets,
        "patch_target_reason": target_reason,
        "error_context": error_context,
        "log_tail": "\n".join(build_log.splitlines()[-160:]),
        "git_status": status,
        "agenda": agenda,
    }

    write_report(report)
    return 0 if code == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
