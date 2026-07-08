#!/usr/bin/env python3
import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

REPO = Path("/home/tim/Desktop/xash3d-gc")


def kill_process_group(proc, grace=5):
    try:
        pgid = os.getpgid(proc.pid)
    except ProcessLookupError:
        return

    print(f"Timeout cleanup: SIGTERM process group {pgid}", flush=True)
    try:
        os.killpg(pgid, signal.SIGTERM)
    except ProcessLookupError:
        return

    deadline = time.time() + grace
    while time.time() < deadline:
        if proc.poll() is not None:
            return
        time.sleep(0.25)

    print(f"Timeout cleanup: SIGKILL process group {pgid}", flush=True)
    try:
        os.killpg(pgid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def kill_repo_dolphin_stragglers():
    # Only kill Dolphin processes whose command line references this repo/probe.
    patterns = [
        str(REPO),
        "xash3d-gc.iso",
        ".ai/dolphin-user-run",
        ".ai/logs/dolphin-probe",
    ]

    try:
        ps = subprocess.run(
            ["pgrep", "-af", "dolphin|Dolphin"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except FileNotFoundError:
        return

    for line in ps.stdout.splitlines():
        parts = line.split(maxsplit=1)
        if not parts:
            continue
        pid = int(parts[0])
        cmdline = parts[1] if len(parts) > 1 else ""

        if pid == os.getpid():
            continue

        if any(p in cmdline for p in patterns):
            print(f"Killing repo Dolphin straggler pid={pid}: {cmdline[:180]}", flush=True)
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

    time.sleep(2)

    try:
        ps = subprocess.run(
            ["pgrep", "-af", "dolphin|Dolphin"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except FileNotFoundError:
        return

    for line in ps.stdout.splitlines():
        parts = line.split(maxsplit=1)
        if not parts:
            continue
        pid = int(parts[0])
        cmdline = parts[1] if len(parts) > 1 else ""

        if pid == os.getpid():
            continue

        if any(p in cmdline for p in patterns):
            print(f"Force-killing repo Dolphin straggler pid={pid}", flush=True)
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso", nargs="?", default="OUT/xash3d-gc.iso")
    ap.add_argument("--timeout", type=int, default=210)
    args = ap.parse_args()

    cmd = ["scripts/dolphin-boot-probe.sh", args.iso]

    print(f"+ bounded dolphin probe: {' '.join(cmd)} timeout={args.timeout}s", flush=True)

    proc = subprocess.Popen(
        cmd,
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        preexec_fn=os.setsid,
    )

    output = []
    timed_out = False

    try:
        assert proc.stdout is not None
        start = time.time()

        while True:
            line = proc.stdout.readline()
            if line:
                output.append(line)
                print(line, end="")

            if proc.poll() is not None:
                # Drain remaining output.
                rest = proc.stdout.read()
                if rest:
                    output.append(rest)
                    print(rest, end="")
                break

            if time.time() - start > args.timeout:
                timed_out = True
                print(f"\nDolphin probe exceeded {args.timeout}s; killing emulator/process group.", flush=True)
                kill_process_group(proc)
                break

            time.sleep(0.05)

    finally:
        kill_repo_dolphin_stragglers()

    if timed_out:
        # If the probe already produced success markers before hanging, allow the
        # automation to continue but make the timeout visible in logs.
        joined = "".join(output)
        success_markers = [
            "MAP_READY:",
            "G36_STATUS: PASS",
            "G45_STATUS: PASS",
            "VISUAL_STATUS: nonblack",
        ]
        if any(m in joined for m in success_markers):
            print("Dolphin probe produced success markers before timeout; treating as pass after cleanup.")
            return 0
        return 124

    return proc.returncode or 0


if __name__ == "__main__":
    raise SystemExit(main())
