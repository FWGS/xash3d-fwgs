"""Parity engine."""

from __future__ import annotations

import csv
import re
from pathlib import Path
from typing import Any

from re_agent.backend.protocol import REBackend
from re_agent.config.schema import ParityConfig, ReAgentConfig
from re_agent.core.models import Finding, GhidraData, HookEntry, ParityStatus, SourceMatch
from re_agent.parity.scoring import score
from re_agent.parity.signals import ALL_SIGNALS
from re_agent.parity.source_indexer import SourceIndexer
from re_agent.utils.address import normalize_address
from re_agent.utils.text import has_fp_asm

HOOK_ADDR_RE = re.compile(r"^0x[0-9a-fA-F]+$")


def read_hooks(path: Path, include_unreversed: bool = False) -> list[HookEntry]:
	out: list[HookEntry] = []
	with path.open("r", encoding="utf-8") as handle:
		reader = csv.DictReader(handle)
		fields = set(reader.fieldnames or [])
		for row in reader:
			address = row.get("address", "").strip()
			if not HOOK_ADDR_RE.match(address):
				continue
			reversed_flag = bool(int(row["reversed"])) if "reversed" in fields and row.get("reversed") else True
			if not include_unreversed and not reversed_flag:
				continue
			class_path = row.get("class", "").strip()
			fn_name = row.get("fn_name", "").strip()
			if not class_path and not fn_name and "name" in fields:
				full_name = row.get("name", "").strip()
				if "::" in full_name:
					class_path, fn_name = full_name.rsplit("::", 1)
				else:
					fn_name = full_name
			out.append(HookEntry(
				class_path=class_path,
				fn_name=fn_name,
				address=address.lower(),
				reversed=reversed_flag,
				locked=bool(int(row["locked"])) if row.get("locked") else False,
				is_virtual=bool(int(row["is_virtual"])) if row.get("is_virtual") else False,
			))
	return out


def fetch_ghidra_data(address: str, backend: REBackend) -> GhidraData:
	data = GhidraData(resolved_address=address)
	try:
		decompile = backend.decompile(address)
		data.decompile_ok = True
		data.callers = decompile.callers
		data.callees = decompile.callees
		data.decompile_has_nan = "nan" in decompile.decompiled.lower()
	except Exception as exc:
		data.decompile_error = str(exc)
	if backend.capabilities.has_asm:
		try:
			asm = backend.get_asm(address)
			if asm is not None:
				data.asm_ok = True
				data.asm_instruction_count = asm.instruction_count
				data.asm_call_count = asm.call_count
				data.asm_has_fp_sensitive = has_fp_asm(asm.instructions)
		except Exception as exc:
			data.asm_error = str(exc)
	return data


def score_single(
	entry: HookEntry,
	source: SourceMatch | None,
	ghidra: GhidraData | None,
	config: ParityConfig,
) -> tuple[ParityStatus, list[Finding]]:
	inline_skip = bool(config.inline_wrapper_autoskip and source and source.is_inline_internal_forwarder)
	findings: list[Finding] = []
	for signal_fn in ALL_SIGNALS:
		finding = signal_fn(source, ghidra, inline_skip, config.call_count_warn_diff)
		if finding is not None:
			findings.append(finding)
	if entry.reversed and source is None and not any(finding.level == "red" for finding in findings):
		findings.append(Finding(level="red", reason="Reversed hook has no source body"))
	return score(findings), findings


def run_parity(
	hooks: list[HookEntry],
	source_root: Path,
	config: ReAgentConfig,
	backend: REBackend | None = None,
	ghidra_data_map: dict[str, GhidraData] | None = None,
) -> list[dict[str, Any]]:
	indexer = SourceIndexer(source_root, config.project_profile)
	results: list[dict[str, Any]] = []
	for entry in hooks:
		addr_key = normalize_address(entry.address)
		source = indexer.find(entry.class_name, entry.fn_name) if entry.fn_name else indexer.find_by_address(entry.address)
		ghidra = ghidra_data_map.get(addr_key) if ghidra_data_map else None
		if ghidra is None and backend is not None:
			try:
				ghidra = fetch_ghidra_data(entry.address, backend)
			except Exception:
				ghidra = None
		status, findings = score_single(entry, source, ghidra, config.parity)
		results.append({
			"hook": entry,
			"status": status,
			"findings": findings,
			"source": source,
			"ghidra": ghidra,
		})
	return results
