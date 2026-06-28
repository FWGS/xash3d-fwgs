#!/usr/bin/env python3
"""One-shot blocker rescue for GameCube port automation stalls."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass(frozen=True)
class RescueRule:
	name: str
	pattern: str
	classification: str
	files: tuple[str, ...]
	read_files: tuple[str, ...]
	instruction: str


@dataclass
class Evidence:
	source: str
	line: str
	mtime: float


RULES = (
	RescueRule(
		"sound decode OOM",
		r"out of memory .*sound/(s_load|soundlib/snd_wav)\.c|snd_wav\.c|s_load\.c",
		"memory_pressure:sound",
		("engine/client/sound/s_load.c", "engine/client/soundlib/snd_wav.c"),
		(".ai/prompts/GAMECUBE_AUDIO_NOTES.md", ".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
		"Make one GameCube-only sound loading or decode memory reduction. Prefer skipping, streaming, or tiny fallback behavior for -gcmap over increasing MEM1 use.",
	),
	RescueRule(
		"client HUD/init OOM",
		r"out of memory .*engine/client/dll_int/cl_game\.c|cl_game\.c",
		"memory_pressure:client_hud",
		("engine/client/dll_int/cl_game.c", "engine/client/cl_main.c"),
		(".ai/prompts/GAMECUBE_MEMORY_BUDGET.md", ".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md"),
		"Make one GameCube-only client/HUD allocation reduction tied to the cited CL/HUD init path. Do not disable the real client wholesale.",
	),
	RescueRule(
		"GX surface cache exhaustion",
		r"D_SCAlloc|cache size|hit the end of memory",
		"memory_pressure:gx_surface_cache",
		("ref/gx/r_surf.c", "ref/gx/r_local.h", "ref/gx/r_main.c"),
		(".ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md", ".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
		"Make one GameCube-only GX surface-cache or fallback-rendering fix. Prefer bounded soft-fail/fallback drawing over fatal Host_Error.",
	),
	RescueRule(
		"resource verification/prespawn blocker",
		r"mislinked resource|CL_RemoveFromResourceList|sendres|resource verification|prespawn",
		"runtime:client_resource_verification",
		("engine/client/parse/cl_parse.c", "engine/client/cl_main.c", "scripts/dolphin-vision-test.py"),
		(".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md", ".ai/prompts/GAMECUBE_MEMORY_BUDGET.md"),
		"Make one GameCube-only client resource verification, prespawn, or evidence fix. Do not hide guest failures in the harness.",
	),
	RescueRule(
		"map/model lookup blocker",
		r"Could not load model maps|maps/.*\.bsp|map lookup|asset lookup",
		"runtime:asset_lookup",
		("filesystem/searchpath.c", "engine/common/model.c", "engine/common/mod_bmodel.c"),
		(".ai/prompts/GAMECUBE_STORAGE_NOTES.md", ".ai/prompts/GOLDSRC_CONTENT_FORMATS.md"),
		"Make one GameCube-only filesystem/model lookup fix. Preserve native GoldSrc paths and do not add host-only paths.",
	),
	RescueRule(
		"read-only log/config write blocker",
		r"Sys_InitLog: can't create|can't create|read-only|demoheader\.tmp|FS_SaveVFSConfig|Host_WriteConfig",
		"storage:read_only_write",
		("engine/common/sys_con.c", "engine/common/filesystem_engine.c", "engine/common/host.c", "filesystem/searchpath.c"),
		(".ai/prompts/GAMECUBE_STORAGE_NOTES.md", ".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md"),
		"Make one GameCube-only read-only media fix. Writes must be skipped, routed to writable storage, or downgraded to diagnostics when storage is unavailable.",
	),
	RescueRule(
		"model token/context blocker",
		r"token/context|context limit|maximum context length|VLLMValidationError|Aider made no edit",
		"automation:model_budget",
		("scripts/ai-goal-loop.py", ".ai/prompts/GAMECUBE_LOCAL_MISSION.md"),
		(".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",),
		"Make one runner/prompt change that narrows context or records the missing source file. Do not broaden the prompt.",
	),
	RescueRule(
		"generic guest fatal",
		r"Host_Error|Sys_Error|GUEST_FAILURE|fatal|panic|assert",
		"runtime:guest_fatal",
		("engine/common/host.c", "engine/platform/gamecube/vid_gamecube.c", "scripts/dolphin-vision-test.py"),
		(".ai/prompts/GAMECUBE_MEMORY_BUDGET.md", ".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md"),
		"Make one source-level fix closest to the fatal path, or improve the fatal breadcrumb if the cause is not actionable from loaded files.",
	),
)


def run(root: Path, args: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
	print("$ " + " ".join(args), flush=True)
	return subprocess.run(args, cwd=root, text=True, check=False, env=env)


def clip(text: str, limit: int = 500) -> str:
	text = re.sub(r"\s+", " ", text).strip()
	return text if len(text) <= limit else text[:limit - 3].rstrip() + "..."


def candidate_logs(root: Path, limit: int = 80) -> list[Path]:
	log_root = root / ".ai/logs"
	if not log_root.is_dir():
		return []
	patterns = (
		"**/result.json", "**/stderr.log", "**/stdout.log", "**/summary.md",
		"aider-pass-*.log", "goal-loop-state.json",
	)
	paths: list[Path] = []
	for pattern in patterns:
		paths.extend(path for path in log_root.glob(pattern) if path.is_file())
	paths = [path for path in paths if "blocker-rescue-" not in path.as_posix()]
	paths = sorted(set(paths), key=lambda path: path.stat().st_mtime, reverse=True)
	return paths[:limit]


def evidence_from_json(path: Path, root: Path) -> list[Evidence]:
	try:
		data = json.loads(path.read_text(encoding="utf-8", errors="replace"))
	except (OSError, json.JSONDecodeError):
		return []
	lines: list[str] = []
	classification = data.get("classification") if isinstance(data, dict) else None
	if isinstance(classification, dict):
		errors = classification.get("errors", [])
		if isinstance(errors, list):
			lines.extend(str(item) for item in errors)
		status = classification.get("status")
		if status and errors:
			lines.append(f"status={status}")
	if isinstance(data, dict):
		for key in ("error", "message"):
			value = data.get(key)
			if value:
				lines.append(str(value))
		next_action = data.get("next_action")
		status_text = str(classification.get("status", "")) if isinstance(classification, dict) else ""
		if next_action and status_text not in {"active_rendering_nonblack"} and re.search(
				r"(?i)blocker|focus|failed|missing|oom|out of memory|fatal|regress",
				str(next_action)):
			lines.append(str(next_action))
	return [Evidence(path.relative_to(root).as_posix(), clip(line), path.stat().st_mtime)
		for line in lines if line.strip()]


def latest_success_cutoff(root: Path) -> float:
	"""Return newest structured runtime success time that makes older blockers stale."""
	cutoff = 0.0
	log_root = root / ".ai/logs"
	if not log_root.is_dir():
		return cutoff
	for path in log_root.glob("dolphin-vision-*/result.json"):
		try:
			data = json.loads(path.read_text(encoding="utf-8", errors="replace"))
		except (OSError, json.JSONDecodeError):
			continue
		classification = data.get("classification") if isinstance(data, dict) else None
		if not isinstance(classification, dict):
			continue
		errors = classification.get("errors", [])
		status = classification.get("status")
		if status == "active_rendering_nonblack" and not errors:
			cutoff = max(cutoff, path.stat().st_mtime)
	return cutoff


def evidence_from_text(path: Path, root: Path) -> list[Evidence]:
	try:
		lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
	except OSError:
		return []
	interesting = re.compile(
		r"(?i)(_Mem_Alloc|out of memory|Host_Error|Sys_Error|GUEST_FAILURE|"
		r"BOOT_FAILURE|FAIL:|fatal|panic|assert|token/context|context limit|"
		r"VLLMValidationError|Aider made no edit|D_SCAlloc|can't create|"
		r"Could not load model|mislinked resource|TIMEOUT)"
	)
	matches = [line for line in lines if interesting.search(line)]
	return [Evidence(path.relative_to(root).as_posix(), clip(line), path.stat().st_mtime)
		for line in matches[-12:]]


def collect_evidence(root: Path) -> list[Evidence]:
	items: list[Evidence] = []
	cutoff = latest_success_cutoff(root)
	for path in candidate_logs(root):
		if cutoff and path.stat().st_mtime < cutoff:
			continue
		if path.suffix == ".json":
			items.extend(evidence_from_json(path, root))
		else:
			items.extend(evidence_from_text(path, root))
	return sorted(items, key=lambda item: item.mtime, reverse=True)


def choose_rule(evidence: list[Evidence]) -> tuple[RescueRule, Evidence | None]:
	if not evidence:
		return RescueRule(
			"no current blocker",
			r"$^",
			"none:no_current_blocker",
			(),
			(),
			"No current blocker was found newer than the latest successful Dolphin evidence.",
		), None
	for item in evidence:
		for rule in RULES:
			if re.search(rule.pattern, item.line, re.IGNORECASE):
				return rule, item
	return RULES[-1], evidence[0] if evidence else None


def existing(paths: tuple[str, ...], root: Path, prefix: str = "") -> list[str]:
	selected: list[str] = []
	for path in paths:
		if (root / path).is_file():
			selected.append(f"{prefix}{path}")
	return selected


def latest_runtime_summary(root: Path) -> str:
	latest = root / ".ai/state/dolphin-harness-latest.md"
	if latest.is_file():
		return clip(latest.read_text(encoding="utf-8", errors="replace"), 1800)
	return "No Dolphin harness summary is recorded."


def write_task(path: Path, rule: RescueRule, evidence: list[Evidence],
		selected: Evidence | None, root: Path) -> None:
	evidence_lines = "\n".join(
		f"- `{item.source}`: {item.line}" for item in evidence[:10]
	) or "- No decisive log evidence found; inspect the newest verifier output."
	selected_line = f"`{selected.source}`: {selected.line}" if selected else "(none)"
	path.write_text(f"""One-shot GameCube blocker rescue.

Classification: {rule.classification}
Selected evidence: {selected_line}

Current Dolphin state:
{latest_runtime_summary(root)}

Recent blocker evidence:
{evidence_lines}

Task:
- {rule.instruction}
- Make exactly one coherent source-level patch using only editable files loaded in this chat.
- Keep the change GameCube-scoped and preserve non-GameCube targets.
- Prefer removing the blocker over adding more probe parsing.
- If the loaded files cannot safely resolve the blocker, update the nearest goal/plan note with the exact missing file or next command; do not mark a goal complete.

Output rules:
- Emit only Aider edit blocks.
- Touch one source file when possible.
- Keep the patch below 160 changed lines.
""", encoding="utf-8")


def write_summary(path: Path, report_path: Path, rule: RescueRule,
		evidence: list[Evidence], selected: Evidence | None, context: list[str],
		read_context: list[str], ran_aider: bool, aider_exit: int | None) -> None:
	with path.open("w", encoding="utf-8") as out:
		out.write("# GameCube Blocker Rescue\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Classification: `{rule.classification}`\n")
		out.write(f"- Rule: {rule.name}\n")
		out.write(f"- Selected evidence: {f'`{selected.source}` {selected.line}' if selected else '(none)'}\n")
		out.write(f"- Report: `{report_path}`\n")
		out.write(f"- Aider run: {'yes' if ran_aider else 'no'}\n")
		if aider_exit is not None:
			out.write(f"- Aider exit: {aider_exit}\n")
		out.write("\n## Context\n\n")
		out.write("- Editable: " + (", ".join(f"`{item}`" for item in context) or "(none)") + "\n")
		out.write("- Read-only: " + (", ".join(f"`{item}`" for item in read_context) or "(none)") + "\n")
		out.write("\n## Evidence\n\n")
		for item in evidence[:20]:
			out.write(f"- `{item.source}`: {item.line}\n")
		out.write("\n## Boundary\n\n")
		out.write(
			"This is a one-shot rescue path for repeated blockers. It should not "
			"replace the goal loop; use it when the loop is retrying the same OOM, "
			"guest fatal, stale probe failure, or context-budget issue.\n"
		)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--run-aider", action="store_true",
		help="run one bounded ai-aider-pass using the generated rescue task")
	parser.add_argument("--log-dir", type=Path)
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"blocker-rescue-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	task_path = log_dir / "task.md"
	report_path = log_dir / "report.json"
	summary_path = log_dir / "summary.md"

	evidence = collect_evidence(root)
	rule, selected = choose_rule(evidence)
	context = existing(rule.files, root)
	read_context = existing(rule.read_files, root, "read:")
	write_task(task_path, rule, evidence, selected, root)

	aider_exit: int | None = None
	if args.run_aider and rule.classification != "none:no_current_blocker":
		env = os.environ.copy()
		env.setdefault("AIDER_AUTOMATION", "1")
		env.setdefault("AI_VERIFY_REQUIRE_DOC_UPDATE", "0")
		env.setdefault("AIDER_OUTPUT_TOKENS_INITIAL", "1024")
		env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_1", "768")
		env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_2", "512")
		env.setdefault("AIDER_OUTPUT_TOKENS_RETRY_3", "384")
		env.setdefault("AIDER_CONTEXT_BYTES_INITIAL", "16000")
		env.setdefault("AIDER_CONTEXT_BYTES_RETRY_1", "10000")
		env.setdefault("AIDER_CONTEXT_BYTES_RETRY_2", "7000")
		env.setdefault("AIDER_CONTEXT_BYTES_RETRY_3", "5000")
		env.setdefault("AIDER_MAX_CHAT_HISTORY_TOKENS", "256")
		env.setdefault("AI_COMMIT_SUBJECT", f"fix: rescue GameCube {rule.classification.replace(':', ' ')}")
		env.setdefault("AI_COMMIT_BODY",
			f"One-shot blocker rescue for {rule.classification}.\n\nEvidence: "
			f"{selected.source if selected else 'none'}")
		result = run(root, ["scripts/ai-aider-pass.sh", str(root), str(task_path),
			*context, *read_context], env=env)
		aider_exit = result.returncode
	elif args.run_aider:
		print("No current blocker found after the latest successful Dolphin evidence; skipping Aider rescue.")

	report_path.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"classification": rule.classification,
		"rule": rule.name,
		"selected_evidence": asdict(selected) if selected else None,
		"evidence": [asdict(item) for item in evidence[:30]],
		"editable_context": context,
		"read_context": read_context,
		"task": str(task_path),
		"ran_aider": args.run_aider,
		"aider_exit": aider_exit,
	}, indent=2) + "\n", encoding="utf-8")
	write_summary(summary_path, report_path, rule, evidence, selected,
		context, read_context, args.run_aider, aider_exit)

	print(f"Blocker rescue summary: {summary_path}")
	print(f"Classification: {rule.classification}")
	if selected:
		print(f"Evidence: {selected.source}: {selected.line}")
	print("Editable context: " + (", ".join(context) if context else "(none)"))
	if args.run_aider and aider_exit not in (None, 0):
		return aider_exit
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
