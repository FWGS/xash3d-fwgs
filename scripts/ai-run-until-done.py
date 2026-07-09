#!/usr/bin/env python3
"""Keep the GameCube goal runner moving until automatic goals are exhausted."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
import time
import tempfile
from itertools import count
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

try:
	import fcntl
except ImportError:  # pragma: no cover - non-Unix fallback
	fcntl = None


DISCOVERY_STATE_PATH = Path(".ai/state/discovery-supervisor.json")
HARNESS_STATE_PATH = Path(".ai/state/dolphin-harness-latest.md")
DISCOVERY_RETRY_RESULTS = {
	10: "no_edit",
	18: "model_budget",
	19: "no_edit",
	20: "runtime_probe",
	21: "runtime_probe",
}
DISCOVERY_FAST_RETRY_STATUSES = {1, 10, 15, 16, 18, 19, 20, 21}
AUTOMATION_DISCOVERY_RESULTS = {"no_edit", "model_budget", "review_reject"}
RUNTIME_DISCOVERY_RESULTS = {
	"runtime_probe",
	"memory_pressure",
	"visual_runtime",
	"audio_runtime",
	"asset_lookup",
}


def run(command: list[str], root: Path, env: dict[str, str] | None = None) -> int:
	print("$ " + " ".join(command), flush=True)
	return subprocess.run(command, cwd=root, env=env or os.environ.copy(),
		check=False).returncode


def acquire_supervisor_lock(root: Path):
	if fcntl is None:
		return None
	lock_path = root / ".ai/goal-supervisor.lock"
	lock_path.parent.mkdir(parents=True, exist_ok=True)
	lock_file = lock_path.open("w", encoding="utf-8")
	try:
		fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
	except BlockingIOError:
		print("run-until-done: another goal supervisor is already running",
			file=sys.stderr)
		lock_file.close()
		return None
	lock_file.write(str(os.getpid()))
	lock_file.truncate()
	lock_file.flush()
	return lock_file


def load_dotenv(path: Path) -> None:
	if not path.is_file():
		return
	for raw_line in path.read_text(encoding="utf-8").splitlines():
		line = raw_line.strip()
		if not line or line.startswith("#") or "=" not in line:
			continue
		if line.startswith("export "):
			line = line[len("export "):].lstrip()
		key, value = line.split("=", 1)
		key = key.strip()
		value = value.strip().strip("'\"")
		if key and key not in os.environ:
			os.environ[key] = value


def api_models_url(api_base: str) -> str:
	parsed = urlparse(api_base)
	if parsed.path.rstrip("/").endswith("/v1"):
		return api_base.rstrip("/") + "/models"
	return api_base.rstrip("/") + "/v1/models"


def model_ready(api_base: str) -> bool:
	request = Request(api_models_url(api_base))
	if os.environ.get("OPENAI_API_KEY"):
		request.add_header("Authorization", f"Bearer {os.environ['OPENAI_API_KEY']}")
	try:
		with urlopen(request, timeout=3) as response:
			return 200 <= response.status < 500
	except (OSError, URLError):
		return False


def read_goals(root: Path) -> list[dict[str, object]]:
	result = subprocess.run(["scripts/ai-goal-loop.py", "--repo", str(root), "--status-json"],
		cwd=root, text=True, capture_output=True, check=False)
	if result.returncode != 0:
		raise RuntimeError(result.stderr.strip() or "goal status failed")
	return json.loads(result.stdout)


def next_automatic_goal(root: Path) -> str | None:
	for goal in read_goals(root):
		if not goal["complete"] and not goal["manual"] and not goal["blocked"]:
			return f"{goal['goal_id']} {goal['title']}"
	return None


def discovered_items(root: Path) -> list[dict[str, object]]:
	result = subprocess.run(
		["python3", "scripts/ai-auto-discover.py", "--repo", str(root), "--json"],
		cwd=root, text=True, capture_output=True, check=False,
	)
	if result.returncode != 0:
		raise RuntimeError(result.stderr.strip() or "auto discovery failed")
	data = json.loads(result.stdout)
	return data if isinstance(data, list) else []


def next_work_item(root: Path, mode: str) -> dict[str, object] | None:
	goal = next_automatic_goal(root)
	if mode == "off":
		return {"kind": "goal", "label": goal} if goal is not None else None

	items = discovered_items(root)
	discovery = next((item for item in items if item.get("kind") == "discovery"), None)

	if mode == "only":
		return discovery
	if mode == "prefer" and discovery is not None:
		return discovery
	if goal is not None:
		return {"kind": "goal", "label": goal}
	if mode in {"after-goals", "prefer"}:
		return discovery
	return None


def record_discovery_feedback(root: Path, item: dict[str, object], status: int,
	result: str, intent: str, observation: str) -> None:
	path = root / DISCOVERY_STATE_PATH
	path.parent.mkdir(parents=True, exist_ok=True)
	repeat_count = 1
	if path.is_file():
		try:
			previous = json.loads(path.read_text(encoding="utf-8"))
		except (OSError, json.JSONDecodeError):
			previous = None
		if isinstance(previous, dict) and \
			previous.get("item_id") == item.get("item_id") and \
			previous.get("result") == result:
			repeat_count = int(previous.get("repeat_count") or 1) + 1
	payload = {
		"goal": item.get("source_goal_id"),
		"item_id": item.get("item_id"),
		"title": item.get("title"),
		"kind": item.get("kind"),
		"exit_code": status,
		"phase": "discovery-pass",
		"result": result,
		"intent": intent,
		"observation": observation,
		"repeat_count": repeat_count,
		"timestamp": datetime.now(timezone.utc).isoformat(),
	}
	path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def clear_discovery_feedback(root: Path) -> None:
	(root / DISCOVERY_STATE_PATH).unlink(missing_ok=True)


def is_runtime_discovery_item(item: dict[str, object]) -> bool:
	if item.get("kind") != "discovery":
		return False
	failure_class = str(item.get("failure_class") or "").strip()
	if failure_class in RUNTIME_DISCOVERY_RESULTS:
		return True
	subject = str(item.get("commit_subject") or "")
	return "GameCube runtime" in subject or "runtime blocker" in subject


def retry_result_for_discovery_item(item: dict[str, object], status: int) -> str:
	if is_runtime_discovery_item(item) and status in DISCOVERY_RETRY_RESULTS:
		return "runtime_probe"
	return DISCOVERY_RETRY_RESULTS.get(status, "runtime_probe")


def refresh_runtime_probe(root: Path, env: dict[str, str]) -> int:
	if os.environ.get("AI_REFRESH_RUNTIME_PROBE", "1").lower() in {"0", "false", "no"}:
		return 0
	if not (root / "scripts/dolphin-boot-probe.sh").is_file():
		print("run-until-done: runtime probe refresh skipped; probe script is missing",
			file=sys.stderr, flush=True)
		return 0
	probe_env = env.copy()
	probe_env.setdefault("DOLPHIN_TIMEOUT", os.environ.get("AI_RUNTIME_PROBE_TIMEOUT", "90"))
	status = run(["scripts/dolphin-boot-probe.sh"], root, env=probe_env)
	if status != 0:
		print(f"run-until-done: runtime probe refresh exited {status}; continuing with refreshed evidence",
			file=sys.stderr, flush=True)
	return status


def runtime_regression_gate(root: Path, env: dict[str, str]) -> int:
	if os.environ.get("AI_RUNTIME_REGRESSION_GATE", "1").lower() in {"0", "false", "no"}:
		return 0
	if not (root / "scripts/gamecube-runtime-regression-gate.py").is_file():
		print("run-until-done: runtime regression gate skipped; gate script is missing",
			file=sys.stderr, flush=True)
		return 0
	return subprocess.run(
		["python3", "scripts/gamecube-runtime-regression-gate.py", "--repo", str(root)],
		cwd=root, env=env, check=False,
	).returncode


def reset_stale_discovery_state(root: Path) -> bool:
	path = root / DISCOVERY_STATE_PATH
	harness = root / HARNESS_STATE_PATH
	if not path.is_file() or not harness.is_file():
		return False
	try:
		payload = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError):
		path.unlink(missing_ok=True)
		return True
	if not isinstance(payload, dict):
		path.unlink(missing_ok=True)
		return True
	result = str(payload.get("result") or "").strip()
	if result not in AUTOMATION_DISCOVERY_RESULTS:
		return False
	path.unlink(missing_ok=True)
	return True


def run_discovery_pass(root: Path, item: dict[str, object]) -> int:
	task = str(item.get("task") or "").strip()
	if not task:
		return 1
	context = [str(path) for path in item.get("context", []) if isinstance(path, str)]
	read_context = [f"read:{path}" for path in item.get("read_context", []) if isinstance(path, str)]
	if is_runtime_discovery_item(item) and context:
		task += (
			"\nRuntime child-process constraint:\n"
			f"- The intended editable file for this pass is exactly `{context[0]}`.\n"
			"- Do not output patches for any other path, demo app, scaffold, or helper package.\n"
			"- If no safe patch exists in that file, output exactly `NO_EDIT`.\n"
		)
	subject = str(item.get("commit_subject") or "chore: apply discovered GameCube fix")
	body = str(item.get("commit_body") or "")
	with tempfile.NamedTemporaryFile("w", suffix=".md", prefix="xash3d-gc-discovery-",
		encoding="utf-8", delete=False) as task_file:
		task_file.write(task)
		task_path = Path(task_file.name)
	try:
		env = os.environ.copy()
		env["AI_COMMIT_SUBJECT"] = subject
		env["AI_COMMIT_BODY"] = body
		env["AI_DIRTY_COMMIT_SUBJECT"] = "chore: checkpoint dirty discovery state"
		env.setdefault("AIDER_AUTOMATION", "1")
		env.setdefault("AI_VERIFY_REQUIRE_DOC_UPDATE", "0")
		env.setdefault("AI_REVIEW_ALLOW_SOURCE_ONLY_DISCOVERY", "1")
		if is_runtime_discovery_item(item):
			env.setdefault("AI_FORBIDDEN_EDIT_PATHS", "engine/platform/gamecube/sys_gamecube.c,re_agent,mathweb,main.py,hello.py")
			env.setdefault("AIDER_PRESERVE_CONTEXT_ORDER", "1")
			env.setdefault("AIDER_CONFIG_PROMPT_SLACK_TOKENS", "8000")
			env.setdefault("AIDER_RESERVED_OUTPUT_SLACK", "1024")
		clear_discovery_feedback(root)
		status = run(["scripts/ai-aider-pass.sh", str(root), str(task_path), *context, *read_context], root, env=env)
		if status != 0:
			result = retry_result_for_discovery_item(item, status)
			intent = {
				10: "Retry from fresh runtime evidence with the same bounded source context.",
				18: "Keep the next pass runtime-focused with reduced context pressure.",
				19: "Restore at least one editable file to the discovery pass before retrying.",
				21: "Discard the forbidden startup-file edit and retry on the loaded runtime file.",
			}.get(status, "Inspect the failed discovery pass before retrying.")
			observation = f"Discovery pass `{item.get('item_id')}` exited {status} before an accepted patch."
			record_discovery_feedback(root, item, status, result, intent, observation)
			return status
		status = run(["scripts/ai-review.sh"], root, env=env)
		if status != 0:
			record_discovery_feedback(
				root,
				item,
				status,
				"review_reject",
				"Align the discovery path with the acceptance gates before retrying source edits.",
				f"Discovery pass `{item.get('item_id')}` built and committed, but the acceptance review rejected the result.",
			)
			return status
		if is_runtime_discovery_item(item):
			probe_status = refresh_runtime_probe(root, env)
			gate_status = runtime_regression_gate(root, env)
			if probe_status != 0 or gate_status != 0:
				record_discovery_feedback(
					root,
					item,
					20,
					"runtime_probe",
					"Restore the GameCube smoke route before accepting another runtime change.",
					f"Discovery pass `{item.get('item_id')}` was rejected by the runtime regression gate.",
				)
				return 20
		clear_discovery_feedback(root)
		return 0
	finally:
		task_path.unlink(missing_ok=True)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--chunk-passes", type=int, default=0,
		help="passes per goal-loop invocation; 0 means unlimited")
	parser.add_argument("--max-cycles", type=int, default=0,
		help="maximum supervisor cycles; 0 means unlimited")
	parser.add_argument("--recoverable-retries", type=int, default=8)
	parser.add_argument("--sleep", type=int, default=20)
	parser.add_argument("--discovery-mode",
		choices=("off", "after-goals", "prefer", "only"),
		default=os.environ.get("AI_AUTO_DISCOVERY", "after-goals"),
		help="how the supervisor should mix fixed goals with evidence-driven discovery")
	args = parser.parse_args()
	root = args.repo.resolve()
	supervisor_lock = acquire_supervisor_lock(root)
	if fcntl is not None and supervisor_lock is None:
		return 2
	load_dotenv(root / ".env")
	if reset_stale_discovery_state(root):
		print("run-until-done: cleared stale automation discovery state; resuming from fresh runtime evidence",
			file=sys.stderr, flush=True)
	os.environ.setdefault("OPENAI_API_KEY", "local")
	api_base = os.environ.get("OPENAI_API_BASE", "http://127.0.0.1:8072/v1")

	if args.chunk_passes < 0:
		parser.error("--chunk-passes must be zero or positive")
	if args.max_cycles < 0:
		parser.error("--max-cycles must be zero or positive")

	cycles = count(1) if args.max_cycles == 0 else range(1, args.max_cycles + 1)
	for cycle in cycles:
		if not model_ready(api_base):
			print(f"run-until-done: model API is not reachable at {api_base}; retrying after {args.sleep}s",
				file=sys.stderr, flush=True)
			time.sleep(args.sleep)
			continue
		work_item = next_work_item(root, args.discovery_mode)
		if work_item is None:
			if args.discovery_mode == "off":
				print("run-until-done: all automatic goals are complete or blocked")
			else:
				print("run-until-done: no automatic goals or discovered work items remain")
			return 0
		cycle_limit = "unlimited" if args.max_cycles == 0 else str(args.max_cycles)
		if work_item.get("kind") == "goal":
			label = str(work_item.get("label"))
			print(f"\n== supervisor cycle {cycle}/{cycle_limit}: {label} ==", flush=True)
			status = run(["scripts/ai-goal-loop.py", "--repo", str(root),
				"--max-passes", str(args.chunk_passes),
				"--recoverable-retries", str(args.recoverable_retries)], root)
		else:
			label = str(work_item.get("title") or work_item.get("item_id") or "discovered task")
			print(f"\n== supervisor cycle {cycle}/{cycle_limit}: discovery - {label} ==", flush=True)
			status = run_discovery_pass(root, work_item)
		if status == 0:
			continue
		if work_item.get("kind") == "goal" and status == 3:
			print(
				"run-until-done: goal loop stopped for review/RC gate after hitting its bounded attempt limit",
				file=sys.stderr,
				flush=True,
			)
			return 3
		if work_item.get("kind") == "discovery" and status in DISCOVERY_FAST_RETRY_STATUSES:
			print(f"run-until-done: child exit {status}; retrying immediately with refreshed discovery state",
				file=sys.stderr, flush=True)
			continue
		print(f"run-until-done: child exit {status}; continuing after {args.sleep}s",
			file=sys.stderr, flush=True)
		time.sleep(args.sleep)
		continue

	if args.max_cycles > 0:
		print("run-until-done: cycle limit reached with automatic goals remaining", file=sys.stderr)
		return 3
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
