#!/usr/bin/env python3
import json
import os
import re
import subprocess
from pathlib import Path

REPO = Path("/home/tim/Desktop/xash3d-gc")

TARGETS = [
    "engine/platform/gamecube/vid_gamecube.c",
    "engine/client/cl_scrn.c",
    "ref/gx/r_main.c",
    "ref/gx/r_surf.c",
    "engine/common/mod_bmodel.c",
]

GC_TARGET_HINTS = (
    "gamecube",
    "gc",
    "dol",
    "boot.dol",
    "iso",
    "xash3d-gc",
)

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

def git_dirty_source_state():
    code, out = run(["git", "status", "--short"])
    dirty_source = [
        line for line in out.splitlines()
        if any(path in line for path in TARGETS)
    ]
    return dirty_source, out

def cmake_targets(build_dir="build"):
    code, out = run(["cmake", "--build", build_dir, "--target", "help"])
    targets = []
    for line in out.splitlines():
        m = re.match(r"\.\.\.\s+(.+)", line.strip())
        if m:
            targets.append(m.group(1).strip())
    return targets, out

def choose_build_command():
    build_dir = "build"
    if not (REPO / build_dir).exists():
        return ["cmake", "--build", build_dir, "-j8"], "missing_build_dir"

    targets, help_log = cmake_targets(build_dir)
    lowered = [(t.lower(), t) for t in targets]

    preferred = []
    for low, real in lowered:
        if any(h in low for h in GC_TARGET_HINTS):
            preferred.append(real)

    # Prefer actual artifacts over broad/all-like targets.
    for needle in ("boot", "dol", "iso", "xash3d-gc", "gamecube"):
        for target in preferred:
            if needle in target.lower():
                return ["cmake", "--build", build_dir, "--target", target, "-j8"], f"target:{target}"

    if preferred:
        target = preferred[0]
        return ["cmake", "--build", build_dir, "--target", target, "-j8"], f"target:{target}"

    # Fallback, but mark it as broad so the report is honest.
    return ["cmake", "--build", build_dir, "-j8"], "fallback_all"

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
    ]

    for i, line in enumerate(lines):
        low = line.lower()
        if any(p.lower() in low for p in patterns):
            start = max(0, i - 12)
            end = min(len(lines), i + 20)
            return "\n".join(lines[start:end])

    return "\n".join(lines[-80:])

def classify_failure(log):
    low = log.lower()

    if "fatal error:" in low and "no such file or directory" in low:
        return "missing_header"
    if "undefined reference" in low or "ld returned" in low or "collect2: error" in low:
        return "linker"
    if "fatal error:" in low or " error:" in low:
        return "compile"
    if "gmake:" in low or "make:" in low:
        return "build_system"
    if "warning:" in low:
        return "warning_only"
    return "runtime_or_unknown"

def main():
    print("== GC reagent ==")

    dirty_source, status = git_dirty_source_state()
    if dirty_source:
        print("Source tree already has edits; refusing to stack patches:")
        print("\n".join(dirty_source))
        return 2

    build_cmd, build_reason = choose_build_command()
    code, build_log = run(build_cmd)

    error_context = extract_error_context(build_log)
    failure = classify_failure(error_context)

    report = {
        "build_ok": code == 0,
        "failure_kind": failure,
        "build_reason": build_reason,
        "build_cmd": build_cmd,
        "targets": TARGETS,
        "error_context": error_context,
        "log_tail": "\n".join(build_log.splitlines()[-120:]),
        "git_status": status,
    }

    out = REPO / ".ai/reagent-last-probe.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print(f"Wrote {out}")
    print(json.dumps(report, indent=2)[:5000])

    return 0 if code == 0 else 1

if __name__ == "__main__":
    raise SystemExit(main())
