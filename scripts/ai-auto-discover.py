#!/usr/bin/env python3
"""Synthesize the next autonomous work item from fresh repo evidence."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from dataclasses import asdict, dataclass
from pathlib import Path


GOAL_RE = re.compile(r"^##\s+(G\d+)\s+\[( |~|x|X|MANUAL|SKIP)\]\s+(.+)$")
PATH_RE = re.compile(r"(?:^|[\s(])((?:\.?\.?/)?(?:[A-Za-z0-9_.-]+/)+[A-Za-z0-9_.-]+)")
WHITESPACE_RE = re.compile(r"\s+")

COMMON_READ_CONTEXT = (
	".ai/prompts/GAMECUBE_LOCAL_MISSION.md",
	".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
	".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
)
DISCOVERY_STATE_PATH = Path(".ai/state/discovery-supervisor.json")
AUTOMATION_FAILURES = {"no_edit", "model_budget", "review_reject"}
RUNTIME_DIRTY_RE = re.compile(r"^(engine/|ref/|common/|filesystem/|public/|stub/)")

DISCOVERY_RECIPES: dict[str, dict[str, object]] = {
	"memory_pressure": {
		"title": "reduce GameCube memory pressure at the current route blocker",
		"subject": "perf: reduce GameCube runtime memory pressure",
		"context": (
			"engine/common/zone.c",
			"engine/common/mod_bmodel.c",
			"engine/common/mod_studio.c",
			"engine/platform/gamecube/vid_gamecube.c",
			"ref/gx/r_surf.c",
		),
		"read_context": (".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",),
	},
	"visual_runtime": {
		"title": "remove the current GameCube visual/runtime blocker",
		"subject": "feat: advance GameCube visual runtime path",
		"context": (
			"engine/platform/gamecube/vid_gamecube.c",
			"engine/client/cl_scrn.c",
			"ref/gx/r_main.c",
			"ref/gx/r_surf.c",
			"ref/gx/r_image.c",
		),
		"read_context": (
			".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md",
			".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",
		),
	},
	"audio_runtime": {
		"title": "remove the current GameCube audio blocker",
		"subject": "feat: advance GameCube audio runtime path",
		"context": (
			"engine/platform/gamecube/snddma_gamecube.c",
			"engine/client/sound/s_main.c",
			"engine/client/sound/s_load.c",
			"engine/client/soundlib/snd_main.c",
		),
		"read_context": (
			".ai/prompts/GAMECUBE_AUDIO_NOTES.md",
			".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",
		),
	},
	"asset_lookup": {
		"title": "fix the latest asset, path, or content staging blocker",
		"subject": "fix: resolve GameCube asset lookup blocker",
		"context": (
			"engine/common/model.c",
			"engine/common/filesystem_engine.c",
			"engine/platform/gamecube/sys_gamecube.c",
			"scripts/build-gamecube-disc.py",
		),
		"read_context": (
			".ai/prompts/GAMECUBE_STORAGE_NOTES.md",
			".ai/prompts/GOLDSRC_CONTENT_FORMATS.md",
		),
	},
	"verification": {
		"title": "repair the latest build or verifier failure",
		"subject": "fix: resolve GameCube verifier failure",
		"context": (
			"scripts/ai-verify.sh",
			"scripts/build-gamecube.sh",
			"scripts/build-gamecube-disc.py",
			"wscript",
		),
		"read_context": (".ai/prompts/GAMECUBE_CONTEXT_INDEX.md",),
	},
	"runtime_probe": {
		"title": "reduce the current GameCube frame/runtime cost",
		"subject": "perf: reduce GameCube runtime frame cost",
		"context": (
			"scripts/dolphin-probe-analyze.py",
			"scripts/gamecube-worst-case-report.py",
			"scripts/dolphin-boot-probe.sh",
			"scripts/gamecube-map-compat-probe.sh",
			"scripts/gamecube-rc-check.sh",
		),
		"read_context": (),
		"include_common_reads": False,
	},
	"no_edit": {
		"title": "unblock the harness from repeated no-edit loops",
		"subject": "chore: tighten GameCube automation context",
		"context": (
			"scripts/ai-run-until-done.py",
		),
		"read_context": (),
		"include_common_reads": False,
	},
	"model_budget": {
		"title": "reduce local-model context pressure for autonomous passes",
		"subject": "chore: reduce GameCube local model budget pressure",
		"context": (
			"scripts/ai-aider-pass.sh",
		),
		"read_context": (),
		"include_common_reads": False,
	},
	"review_reject": {
		"title": "align discovery passes with the acceptance gates",
		"subject": "chore: align GameCube discovery acceptance gates",
		"context": (
			"scripts/ai-run-until-done.py",
		),
		"read_context": (),
		"include_common_reads": False,
	},
}


@dataclass
class Goal:
	goal_id: str
	state: str
	title: str
	body: str

	@property
	def complete(self) -> bool:
		return self.state.lower() == "x"

	@property
	def manual(self) -> bool:
		return self.state == "MANUAL"

	@property
	def skipped(self) -> bool:
		return self.state == "SKIP"

	@property
	def blocked(self) -> bool:
		return bool(re.search(r"(?im)^\s*-\s*Status:\s*BLOCKED\b", self.body))

	@property
	def automatic_done(self) -> bool:
		return self.complete or self.manual or self.skipped or self.blocked


@dataclass
class WorkItem:
	kind: str
	item_id: str
	title: str
	priority: int
	reason: str
	task: str
	context: list[str]
	read_context: list[str]
	commit_subject: str
	commit_body: str
	source_goal_id: str | None = None
	failure_class: str | None = None


def parse_goals(path: Path) -> list[Goal]:
	goals: list[Goal] = []
	current: tuple[str, str, str] | None = None
	body: list[str] = []
	for line in path.read_text(encoding="utf-8").splitlines():
		match = GOAL_RE.match(line)
		if match:
			if current:
				goals.append(Goal(*current, "\n".join(body).strip()))
			current = match.groups()
			body = []
		elif current:
			body.append(line)
	if current:
		goals.append(Goal(*current, "\n".join(body).strip()))
	return goals


def active_goal(goals: list[Goal]) -> Goal | None:
	return next((goal for goal in goals if not goal.automatic_done), None)


def load_memory(root: Path) -> dict[str, object]:
	path = root / ".ai/state/goal-loop-memory.json"
	if not path.is_file():
		return {}
	try:
		data = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError):
		return {}
	return data if isinstance(data, dict) else {}


def load_discovery_state(root: Path) -> dict[str, object] | None:
	path = root / DISCOVERY_STATE_PATH
	if not path.is_file():
		return None
	try:
		data = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError):
		return None
	return data if isinstance(data, dict) else None


def dirty_runtime_paths(root: Path) -> list[str]:
	result = subprocess.run(
		["git", "status", "--short"],
		cwd=root,
		text=True,
		capture_output=True,
		check=False,
	)
	if result.returncode != 0:
		return []
	paths: list[str] = []
	for raw_line in result.stdout.splitlines():
		line = raw_line.rstrip()
		if not line:
			continue
		path = line[3:]
		if " -> " in path:
			path = path.split(" -> ", 1)[1]
		if RUNTIME_DIRTY_RE.match(path):
			paths.append(path)
	return paths


def normalize_discovery_state(root: Path, state: dict[str, object]) -> dict[str, object]:
	result = str(state.get("result") or "").strip()
	repeat_count = int(state.get("repeat_count") or 1)
	runtime_dirty = dirty_runtime_paths(root)
	if result in {"model_budget", "review_reject"} and runtime_dirty:
		state = dict(state)
		state["result"] = "runtime_probe"
		state["intent"] = "Dirty engine or runtime files already exist; prefer a bounded runtime source pass over more automation repair."
		state["observation"] = (
			"Dirty runtime paths already exist in the worktree: " +
			", ".join(runtime_dirty[:4]) +
			(" ..." if len(runtime_dirty) > 4 else "") +
			". Return to a bounded runtime source patch instead of mixing more automation edits."
		)
		return state
	if result in {"model_budget", "review_reject"} and (root / ".ai/state/dolphin-harness-latest.md").is_file():
		state = dict(state)
		state["result"] = "runtime_probe"
		state["intent"] = "Automation recovery signal received; resume the smallest GameCube runtime source pass instead of editing the supervisor."
		state["observation"] = (
			f"Discovery recovery result `{result}` repeated {repeat_count} time(s); "
			"treat it as a cue to return to fresh runtime evidence rather than spending another cycle on automation files."
		)
	elif result == "model_budget" and repeat_count >= 2:
		state = dict(state)
		state["result"] = "runtime_probe"
		state["intent"] = "Automation budget repair plateaued; resume the smallest engine-side runtime fix that fits the local model."
		state["observation"] = (
			f"Repeated model-budget repair loop detected ({repeat_count} consecutive passes); "
			"return to a bounded runtime source patch instead of spending more cycles on the automation harness."
		)
	elif result == "review_reject" and repeat_count >= 2:
		state = dict(state)
		state["result"] = "runtime_probe"
		state["intent"] = "Acceptance-gate repair plateaued; resume the smallest engine-side runtime fix and verify whether the source path now lands cleanly."
		state["observation"] = (
			f"Repeated review-reject loop detected ({repeat_count} consecutive passes); "
			"return to a bounded runtime source patch instead of extending the automation review path again."
		)
	return state


def latest_recent_step(memory: dict[str, object]) -> dict[str, object] | None:
	conact = memory.get("conact")
	if not isinstance(conact, dict):
		return None
	recent = conact.get("recent_step_record")
	if not isinstance(recent, list):
		return None
	for item in reversed(recent):
		if isinstance(item, dict):
			return item
	return None


def sanitize_for_prompt(text: str, *, limit: int) -> str:
	text = text.replace("\x00", " ")
	text = PATH_RE.sub(lambda match: match.group(0).replace(match.group(1), "[path]"), text)
	text = text.replace("SEARCH/REPLACE", "patch block")
	text = WHITESPACE_RE.sub(" ", text).strip()
	if len(text) <= limit:
		return text
	cut = text[:limit].rstrip()
	if " " in cut:
		cut = cut.rsplit(" ", 1)[0]
	return cut + "..."


def runtime_summary(root: Path) -> str:
	latest = root / ".ai/state/dolphin-harness-latest.md"
	if latest.is_file():
		text = latest.read_text(encoding="utf-8", errors="replace").strip()
		if text:
			status = re.search(r"(?m)^- Status:\s*(.+)$", text)
			analysis = re.search(r"(?m)^- Analysis:\s*(.+)$", text)
			model = re.search(r"(?s)## Model Analysis\s*(.+)$", text)
			parts = []
			if status:
				parts.append(f"Status={status.group(1).strip()}")
			if analysis:
				parts.append(f"Analysis={analysis.group(1).strip()}")
			if model:
				parts.append(model.group(1).strip())
			return sanitize_for_prompt(" ".join(parts) or text, limit=320)
	return "No Dolphin runtime summary recorded yet."


def existing_paths(root: Path, paths: tuple[str, ...]) -> list[str]:
	return [path for path in paths if (root / path).is_file()]


def sort_paths_by_size(root: Path, paths: list[str]) -> list[str]:
	return sorted(paths, key=lambda path: ((root / path).stat().st_size, path))


def runtime_port_status(root: Path) -> str:
	parts: list[str] = []
	if (root / "OUT/bin/boot.dol").is_file() and (root / "OUT/xash3d-gc.iso").is_file():
		parts.append("The native build/package path is already working (`OUT/bin/boot.dol` and `OUT/xash3d-gc.iso` exist).")
	parts.append("What remains is runtime porting: boot cleanly, load a map, reach controller-ready, confirm stable visual output, confirm stable audio, and avoid fatal/runtime regressions.")
	parts.append("Do not spend this pass on supervisor or review-gate tuning.")
	return " ".join(parts)


def build_discovered_item(root: Path, goal: Goal | None, recent: dict[str, object]) -> WorkItem | None:
	failure_class = str(recent.get("result") or "").strip() or "runtime_probe"
	recipe = DISCOVERY_RECIPES.get(failure_class)
	if recipe is None and goal is not None:
		recipe = DISCOVERY_RECIPES.get("runtime_probe")
		failure_class = "runtime_probe"
	if recipe is None:
		return None
	context = existing_paths(root, tuple(str(path) for path in recipe["context"]))
	if failure_class in {"runtime_probe", "no_edit", "review_reject"} and context:
		context = sort_paths_by_size(root, context)[:1]
	read_context = existing_paths(root, tuple(str(path) for path in recipe["read_context"]))
	if recipe.get("include_common_reads", True):
		read_context.extend(path for path in COMMON_READ_CONTEXT if (root / path).is_file() and path not in read_context)
	context_list = "\n".join(f"- {path}" for path in context) or "- none"
	goal_label = f"{goal.goal_id} {goal.title}" if goal is not None else "the final GameCube port objective"
	observation = sanitize_for_prompt(
		str(recent.get("observation") or "").strip() or "No captured observation.",
		limit=280,
	)
	intent = sanitize_for_prompt(
		str(recent.get("intent") or "").strip() or "Use the freshest runtime evidence.",
		limit=140,
	)
	reason = f"Recent evidence classified the blocker as `{failure_class}` while advancing {goal_label}."
	evidence_heading = "Fresh runtime evidence"
	evidence_body = runtime_summary(root)
	editable_guidance = (
		f"Loaded editable files:\n{context_list}\n\n"
		"If the loaded editable file list above is not empty, choose from that list and do not claim that no editable files were provided.\n\n"
	)
	objective_guidance = ""
	if failure_class in AUTOMATION_FAILURES:
		evidence_heading = "Automation evidence"
		evidence_body = "The latest accepted path stalled in the supervisor or harness layer; prefer a minimal automation fix or return to the smallest runtime source patch."
	else:
		frame_budget_guidance = ""
		if "Captured " in evidence_body and "frame timing sample" in evidence_body and "avg=" in evidence_body:
			frame_budget_guidance = (
				"Frame-budget guidance:\n"
				"- Timing samples exist; this is over-budget telemetry, not missing telemetry.\n"
				"- Do not edit startup, clock, DVD, or base-path code.\n"
				"- Reduce frame/render cost while preserving MAP_READY/G45/nonblack.\n\n"
			)
		objective_guidance = (
			"Porting priority:\n"
			f"{runtime_port_status(root)}\n\n"
			f"{frame_budget_guidance}"
			"Runtime loop: use current probe evidence, make one source patch, do not change automation policy.\n\n"
		)
		editable_guidance = (
			"Act only on editable files already loaded in chat.\n\n"
		)
	task = f"""Advance the GameCube port with one bounded autonomous pass.

Objective: {goal_label}
Blocker: {failure_class}
Reason: {reason}
Intent: {intent}
Observation: {observation}

{evidence_heading}:
{evidence_body}

{objective_guidance}{editable_guidance}Task:
- Make one small source-first patch in loaded editable files.
- Prefer reducing the current blocker over instrumentation.
- Touch one editable file unless a second is required to build.
- No docs-only updates or manual questions.

Output rules:
- Return only SEARCH/REPLACE blocks or NO_EDIT.
- No prose, checklist, plan, markdown fences, or explanations.
- Keep the reply under 60 lines.
"""
	return WorkItem(
		kind="discovery",
		item_id=f"discovery:{failure_class}" + (f":{goal.goal_id}" if goal is not None else ""),
		title=str(recipe["title"]),
		priority=80,
		reason=reason,
		task=task,
		context=context,
		read_context=read_context,
		commit_subject=str(recipe["subject"]),
		commit_body="\n".join((
			f"Discovered blocker: {failure_class}",
			f"Objective: {goal_label}",
			"",
			"Reasoning source:",
			f"- Latest autonomous observation: {observation[:240]}",
			f"- Latest autonomous intent: {intent[:240]}",
			"",
			"This commit came from the auto-discovery supervisor path rather than a fixed ledger step.",
		)),
		source_goal_id=goal.goal_id if goal is not None else None,
		failure_class=failure_class,
	)


def build_goal_item(goal: Goal) -> WorkItem:
	return WorkItem(
		kind="goal",
		item_id=goal.goal_id,
		title=goal.title,
		priority=50,
		reason="Next automatic ledger goal.",
		task="",
		context=[],
		read_context=[],
		commit_subject="",
		commit_body="",
		source_goal_id=goal.goal_id,
	)


def discover_items(root: Path) -> list[WorkItem]:
	goals = parse_goals(root / ".ai/goals/GAMECUBE_PORT_GOALS.md")
	goal = active_goal(goals)
	items: list[WorkItem] = []
	if goal is not None:
		items.append(build_goal_item(goal))
	recent = load_discovery_state(root) or latest_recent_step(load_memory(root))
	if recent is not None:
		recent = normalize_discovery_state(root, recent)
	if recent is not None:
		discovered = build_discovered_item(root, goal, recent)
		if discovered is not None:
			items.append(discovered)
	return sorted(items, key=lambda item: (-item.priority, item.item_id))


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--json", action="store_true", help="emit machine-readable work items")
	args = parser.parse_args()
	items = discover_items(args.repo.expanduser().resolve())
	if args.json:
		print(json.dumps([asdict(item) for item in items], indent=2))
		return 0
	if not items:
		print("No discovered work items.")
		return 0
	for item in items:
		print(f"{item.kind}\t{item.item_id}\t{item.priority}\t{item.title}")
		print(f"  reason: {item.reason}")
		if item.context:
			print(f"  context: {', '.join(item.context)}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
