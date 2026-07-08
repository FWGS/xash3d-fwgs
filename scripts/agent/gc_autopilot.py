#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path

REPO = Path("/home/tim/Desktop/xash3d-gc")
REPORT = REPO / ".ai/reagent-last-probe.json"
TASK_OUT = REPO / ".ai/next-patch-task.txt"


def run(cmd, *, check=False):
    print("+", " ".join(cmd), flush=True)
    p = subprocess.run(
        cmd,
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    print(p.stdout)
    if check and p.returncode != 0:
        raise SystemExit(p.returncode)
    return p.returncode, p.stdout


def load_report():
    if not REPORT.exists():
        return None
    return json.loads(REPORT.read_text(encoding="utf-8"))


def git_changed_files():
    code, out = run(["git", "status", "--short"])
    files = []
    for line in out.splitlines():
        if len(line) > 3:
            files.append(line[3:].strip())
    return files


def commit_changes(message):
    changed = git_changed_files()
    if not changed:
        print("No changes to commit.")
        return False

    run(["git", "add", "-A"], check=True)
    code, out = run(["git", "commit", "-m", message])
    return code == 0


def run_reagent():
    code, _ = run(["python3", "scripts/agent/gc_reagent.py"])
    report = load_report()
    if report is None:
        raise SystemExit("No reagent report produced.")
    return code, report


def patch_cl_scrn_bad_gamecube_block():
    path = REPO / "engine/client/cl_scrn.c"
    if not path.exists():
        path = REPO / "engine/client/cl_scrn.cc"

    if not path.exists():
        return False, "engine/client/cl_scrn.c not found"

    text = path.read_text(encoding="utf-8")

    needle = "if( gc.width >= 160 && gc.height >= 120 )"
    reset = "memset( &clgame.ds, 0, sizeof( clgame.ds ));"

    if needle not in text:
        return False, "gc.width broken SCR_VidInit block not found"

    needle_i = text.index(needle)
    reset_i = text.find(reset, needle_i)
    if reset_i == -1:
        return False, "draw-state reset not found after broken SCR_VidInit block"

    # Remove optional comment immediately before the bad block.
    remove_start = text.rfind("/*", 0, needle_i)
    if remove_start == -1 or remove_start < text.rfind("#endif", 0, needle_i):
        remove_start = text.rfind("\n", 0, needle_i) + 1

    # Find the GameCube guard that encloses the bad block.
    guard_i = text.rfind("#if XASH_GAMECUBE", 0, needle_i)
    endif_i = text.rfind("#endif", 0, needle_i)

    before = text[:remove_start].rstrip()
    after = text[reset_i:]

    # If the bad block lives inside an unclosed #if XASH_GAMECUBE, close it.
    if guard_i != -1 and (endif_i == -1 or endif_i < guard_i):
        patched = before + "\n#endif\n\n        " + after.lstrip()
    else:
        patched = before + "\n\n        " + after.lstrip()

    if patched == text:
        return False, "patch produced no change"

    path.write_text(patched, encoding="utf-8")
    print(f"Patched {path.relative_to(REPO)}: removed bad gc.width SCR_VidInit block")
    return True, str(path.relative_to(REPO))

def apply_known_fix(report):
    failure = report.get("failure_kind")
    targets = report.get("patch_targets", [])
    error_context = report.get("error_context", "")

    if (
        failure == "compile"
        and "engine/client/cl_scrn.c" in targets
        and "gc.width" in error_context
        and "unterminated #if" in error_context
    ):
        ok, detail = patch_cl_scrn_bad_gamecube_block()
        if ok:
            return True, "fix: remove broken GameCube SCR_VidInit loop"
        return False, detail

    return False, "no_known_deterministic_fix"


def write_ai_task(report):
    targets = [
        t for t in report.get("patch_targets", [])
        if not t.startswith("public/miniz.c")
    ]

    TASK_OUT.parent.mkdir(parents=True, exist_ok=True)
    TASK_OUT.write_text(
        "Auto-port task for Xash3D GameCube\n"
        "===================================\n\n"
        f"Failure kind: {report.get('failure_kind')}\n"
        f"Build phase: {report.get('build_reason')}\n"
        f"Build command: {' '.join(report.get('build_cmd') or [])}\n"
        f"Patch targets: {targets}\n\n"
        "Rules:\n"
        "- Patch only the first real failure file.\n"
        "- Do not touch engine/platform/gamecube/vid_gamecube.c unless the failure names it.\n"
        "- Do not patch generated build/ files.\n"
        "- Do not patch public/miniz.c when it is only a compiler note.\n"
        "- Keep the patch tiny and compile-driven.\n\n"
        "Error context:\n"
        "--------------\n"
        f"{report.get('error_context', '')}\n",
        encoding="utf-8",
    )
    print(f"Wrote {TASK_OUT.relative_to(REPO)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-steps", type=int, default=8)
    args = ap.parse_args()

    for step in range(1, args.max_steps + 1):
        print(f"\n=== GC AUTOPILOT STEP {step}/{args.max_steps} ===")

        code, report = run_reagent()

        if report.get("build_ok"):
            print("GameCube pipeline passed current reagent phases.")
            return 0

        ok, message = apply_known_fix(report)
        if ok:
            run(["git", "diff", "--", "engine/client/cl_scrn.c", "engine/client/cl_scrn.cc"])
            commit_changes(message)
            continue

        write_ai_task(report)
        print("No deterministic fixer matched this failure yet.")
        print("Next: feed .ai/next-patch-task.txt plus the first patch target to the AI patcher.")
        return 3

    print("Reached max autopilot steps.")
    return 4


if __name__ == "__main__":
    raise SystemExit(main())
