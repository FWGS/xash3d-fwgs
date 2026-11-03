#!/usr/bin/env bash
# Cleanup only processes using this probe's private Dolphin profile.
cleanup_boot_probe_processes() {
	python3 - "${USER_DIR:-}" <<'PY'
import os, signal, sys, time

profile = os.path.realpath(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1] else ""
if not profile:
	raise SystemExit(0)

def owned_pids():
	found = []
	for entry in os.scandir("/proc"):
		if not entry.name.isdigit():
			continue
		try:
			with open(f"/proc/{entry.name}/cmdline", "rb") as f:
				argv = [arg.decode(errors="replace") for arg in f.read().split(b"\0") if arg]
		except (OSError, ProcessLookupError):
			continue
		if not argv or os.path.basename(argv[0]).lower() not in {
			"dolphin-emu", "dolphinqt", "xash-gc-dolphin", "flatpak"
		}:
			continue
		for i, arg in enumerate(argv[:-1]):
			if arg == "-u" and os.path.realpath(argv[i + 1]) == profile:
				found.append(int(entry.name))
				break
	return found

pids = owned_pids()
for pid in pids:
	try: os.kill(pid, signal.SIGTERM)
	except ProcessLookupError: pass
deadline = time.monotonic() + 1.5
while pids and time.monotonic() < deadline:
	time.sleep(0.1)
	pids = [pid for pid in pids if os.path.exists(f"/proc/{pid}")]
for pid in pids:
	try: os.kill(pid, signal.SIGKILL)
	except ProcessLookupError: pass
PY
}
cleanup_boot_probe_locks() {
	# flock releases automatically; preserve its inode for concurrent probes.
	:
}
on_boot_probe_exit() {
	rc=$?
	cleanup_boot_probe_processes
	cleanup_boot_probe_locks
	exit "$rc"
}
