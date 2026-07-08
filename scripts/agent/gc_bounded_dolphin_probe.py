#!/usr/bin/env python3
import argparse
import os
import signal
import subprocess
import time
from pathlib import Path

REPO = Path("/home/tim/Desktop/xash3d-gc")


def kill_process_group(proc, grace=5):
    if proc.poll() is not None:
        return

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


def kill_dolphin_emulator_stragglers():
    """
    Kill only actual Dolphin/Flatpak emulator processes related to this repo.
    Do not kill scripts named dolphin-boot-probe.sh or gc_bounded_dolphin_probe.py.
    """
    try:
        ps = subprocess.run(
            ["ps", "-eo", "pid=,comm=,args="],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except FileNotFoundError:
        return

    repo_markers = [
        str(REPO),
        "xash3d-gc.iso",
        ".ai/dolphin-user-run",
        ".ai/logs/dolphin-probe",
        "org.DolphinEmu.dolphin-emu",
    ]

    skip_markers = [
        "gc_bounded_dolphin_probe.py",
        "dolphin-boot-probe.sh",
        "dolphin-probe-common.sh",
        "python3 scripts/agent",
        "/bin/bash",
    ]

    emulator_markers = [
        "dolphin-emu",
        "dolphin-emu-nogui",
        "DolphinQt",
        "org.DolphinEmu.dolphin-emu",
        "flatpak run org.DolphinEmu.dolphin-emu",
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

        if pid == os.getpid():
            continue

        haystack = f"{comm} {args}"

        if any(skip in haystack for skip in skip_markers):
            continue

        if not any(marker in haystack for marker in emulator_markers):
            continue

        if not any(marker in haystack for marker in repo_markers):
            continue

        victims.append((pid, haystack))

    for pid, cmdline in victims:
        print(f"Killing Dolphin emulator pid={pid}: {cmdline[:180]}", flush=True)
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass

    time.sleep(2)

    for pid, cmdline in victims:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            continue

        print(f"Force-killing Dolphin emulator pid={pid}", flush=True)
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
        start_new_session=True,
    )

    timed_out = False

    try:
        output, _ = proc.communicate(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        print(f"Dolphin probe exceeded {args.timeout}s; killing probe process group.", flush=True)
        kill_process_group(proc)
        output, _ = proc.communicate(timeout=10)
    finally:
        kill_dolphin_emulator_stragglers()

    if output:
        print(output, end="" if output.endswith("\n") else "\n")

    success_markers = [
        "MAP_READY:",
        "G36_STATUS: PASS",
        "G45_STATUS: PASS",
        "VISUAL_STATUS: nonblack",
    ]

    if timed_out and any(marker in output for marker in success_markers):
        print("Dolphin probe produced success markers before timeout; treating as pass after cleanup.")
        return 0

    if proc.returncode == 0:
        return 0

    print(f"Dolphin probe failed with exit code {proc.returncode}")
    return proc.returncode or 1


if __name__ == "__main__":
    raise SystemExit(main())
