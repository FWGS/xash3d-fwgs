"""Report-first analysis for suspicious binary functions."""

from __future__ import annotations

import subprocess
from dataclasses import asdict
from pathlib import Path
from typing import Any

from re_agent.config.schema import ReAgentConfig
from re_agent.core.models import FunctionTarget, ParityStatus
from re_agent.parity.engine import fetch_ghidra_data, score_single
from re_agent.parity.source_indexer import SourceIndexer


SOURCE_STATUS_PATHS = ("engine", "ref", "common", "public", "re_agent")


def _source_dirty(root: Path) -> list[str]:
	result = subprocess.run(
		["git", "status", "--porcelain", "--", *SOURCE_STATUS_PATHS],
		cwd=root,
		text=True,
		capture_output=True,
		check=False,
	)
	return [line for line in result.stdout.splitlines() if line.strip()]


def _confidence(source, ghidra, parity_status: ParityStatus) -> tuple[float, str]:
	score = 0.0
	if ghidra is not None and ghidra.decompile_ok:
		score += 0.30
	if source is not None:
		score += 0.25
	if ghidra is not None and ghidra.asm_ok:
		score += 0.15
	if parity_status == ParityStatus.GREEN:
		score += 0.30
	elif parity_status == ParityStatus.YELLOW:
		score += 0.15
	label = "high" if score >= 0.80 else "medium" if score >= 0.55 else "low"
	return round(score, 2), label


def analyze_function(
	root: Path,
	address: str,
	config: ReAgentConfig,
	backend,
) -> dict[str, Any]:
	source_root = Path(config.project_profile.source_root)
	if not source_root.is_absolute():
		source_root = root / source_root

	decompile = None
	decompile_error = None
	try:
		decompile = backend.decompile(address)
		extracted_name = decompile.name or address
	except Exception as exc:
		extracted_name = address
		decompile_error = str(exc)

	class_name = ""
	function_name = extracted_name
	if "::" in extracted_name:
		class_name, _, function_name = extracted_name.rpartition("::")
	target = FunctionTarget(address=address, class_name=class_name, function_name=function_name)
	indexer = SourceIndexer(source_root, config.project_profile)
	source = indexer.find(class_name, function_name) if function_name else None
	ghidra = fetch_ghidra_data(address, backend) if decompile is not None else None
	entry = target_to_hook(target)
	parity_status, findings = score_single(entry, source, ghidra, config.parity)
	confidence_value, confidence_label = _confidence(source, ghidra, parity_status)

	report: dict[str, Any] = {
		"schema": "re-agent.investigation.v1",
		"address": address,
		"symbol": extracted_name,
		"confidence": {"score": confidence_value, "label": confidence_label},
		"parity": {
			"status": parity_status.value,
			"findings": [asdict(finding) for finding in findings],
		},
		"decompile": (
			{
				"name": decompile.name,
				"signature": decompile.signature,
				"body": decompile.decompiled,
				"callers": decompile.callers,
				"callees": decompile.callees,
			}
			if decompile is not None else {"error": decompile_error}
		),
		"source_match": (
			{
				"path": source.path,
				"line": source.line,
				"body": source.body,
				"body_lines": source.body_lines,
				"call_count": source.call_count,
				"control_flow_count": source.control_flow_count,
				"has_stub_marker": source.has_stub_marker,
			}
			if source is not None else None
		),
		"validation": {
			"requested": False,
			"accepted": False,
			"build": {"status": "not_run"},
			"dolphin": {"status": "not_run"},
			"source_changes": [],
		},
		"acceptance": {
			"accepted": False,
			"rule": "A report is not accepted unless build and Dolphin validation both pass.",
		},
	}
	return report


def target_to_hook(target: FunctionTarget):
	from re_agent.core.models import HookEntry

	return HookEntry(
		class_path=target.class_name,
		fn_name=target.function_name,
		address=target.address,
		reversed=True,
		locked=False,
		is_virtual=False,
	)


def validate_report(root: Path, report: dict[str, Any], timeout_s: int) -> dict[str, Any]:
	validation = report["validation"]
	validation["requested"] = True
	initial_dirty = _source_dirty(root)
	if initial_dirty:
		validation["source_changes"] = initial_dirty
		validation["error"] = "Source tree is dirty; validation refused."
		return validation

	log_dir = root / ".ai/logs/re-agent-validation"
	log_dir.mkdir(parents=True, exist_ok=True)
	commands = (
		("build", ["scripts/build-gamecube.sh"]),
		("dolphin", ["scripts/dolphin-boot-probe.sh"]),
	)
	for key, command in commands:
		log_path = log_dir / f"{key}.log"
		try:
			completed = subprocess.run(
				command,
				cwd=root,
				text=True,
				stdout=subprocess.PIPE,
				stderr=subprocess.STDOUT,
				env={**__import__("os").environ, "DOLPHIN_TIMEOUT": str(timeout_s)},
				timeout=timeout_s,
				check=False,
			)
			log_path.write_text(completed.stdout, encoding="utf-8")
			validation[key] = {
				"status": "pass" if completed.returncode == 0 else "fail",
				"exit_code": completed.returncode,
				"command": command,
				"log": str(log_path.relative_to(root)),
			}
			if completed.returncode != 0:
				break
		except subprocess.TimeoutExpired as exc:
			output = exc.stdout or ""
			if isinstance(output, bytes):
				output = output.decode("utf-8", errors="replace")
			log_path.write_text(output, encoding="utf-8")
			validation[key] = {
				"status": "timeout",
				"command": command,
				"log": str(log_path.relative_to(root)),
			}
			break

	validation["source_changes"] = _source_dirty(root)
	build_ok = validation.get("build", {}).get("status") == "pass"
	dolphin_ok = validation.get("dolphin", {}).get("status") == "pass"
	validation["accepted"] = bool(build_ok and dolphin_ok and not validation["source_changes"])
	report["acceptance"]["accepted"] = validation["accepted"]
	return validation
