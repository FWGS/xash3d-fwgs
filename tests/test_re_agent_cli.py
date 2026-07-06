from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path

from re_agent.cli.main import build_parser, main
from re_agent.config.loader import load_config
from re_agent.core.models import FunctionTarget
from re_agent.core.session import Session
from re_agent.parity.engine import read_hooks, run_parity
from re_agent.verification.objective import verify_candidate


class ReAgentCliTests(unittest.TestCase):
	def test_parser_builds(self) -> None:
		self.assertIsNotNone(build_parser())

	def test_no_command_returns_zero(self) -> None:
		self.assertEqual(main([]), 0)

	def test_init_creates_config(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			config_path = Path(tmpdir) / "re-agent.yaml"
			self.assertEqual(main(["--config", str(config_path), "init"]), 0)
			self.assertTrue(config_path.exists())
			json.loads(config_path.read_text(encoding="utf-8"))

	def test_init_fails_if_exists(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			config_path = Path(tmpdir) / "re-agent.yaml"
			config_path.write_text("{}", encoding="utf-8")
			self.assertEqual(main(["--config", str(config_path), "init"]), 1)

	def test_status_no_session(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			config_path = Path(tmpdir) / "re-agent.yaml"
			config_path.write_text(json.dumps({
				"output": {
					"session_file": str(Path(tmpdir) / "progress.json"),
					"report_dir": str(Path(tmpdir) / "reports"),
					"log_dir": str(Path(tmpdir) / "logs"),
				}
			}), encoding="utf-8")
			self.assertEqual(main(["--config", str(config_path), "status"]), 0)

	def test_reverse_dry_run(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			config_path = Path(tmpdir) / "re-agent.yaml"
			config_path.write_text("{}", encoding="utf-8")
			self.assertEqual(main(["--config", str(config_path), "reverse", "--address", "0x6F86A0", "--dry-run"]), 0)

	def test_reverse_no_target(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			config_path = Path(tmpdir) / "re-agent.yaml"
			config_path.write_text("{}", encoding="utf-8")
			self.assertEqual(main(["--config", str(config_path), "reverse"]), 1)

	def test_env_override(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			config_path = Path(tmpdir) / "re-agent.yaml"
			config_path.write_text("{}", encoding="utf-8")
			old = os.environ.get("RE_AGENT_LLM_PROVIDER")
			try:
				os.environ["RE_AGENT_LLM_PROVIDER"] = "stub"
				config = load_config(config_path)
				self.assertEqual(config.llm.provider, "stub")
			finally:
				if old is None:
					os.environ.pop("RE_AGENT_LLM_PROVIDER", None)
				else:
					os.environ["RE_AGENT_LLM_PROVIDER"] = old


class ReAgentCoreTests(unittest.TestCase):
	def test_session_records_result(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			session = Session(Path(tmpdir) / "progress.json")
			from re_agent.core.models import CheckerVerdict, ReversalResult, Verdict
			result = ReversalResult(
				target=FunctionTarget(address="0x10", class_name="CTest", function_name="Run"),
				code="void Run() {}",
				checker_verdict=CheckerVerdict(verdict=Verdict.PASS, summary="ok"),
				rounds_used=1,
				success=True,
			)
			session.record_result(result)
			self.assertTrue(session.is_completed("0x10"))
			self.assertEqual(session.get_summary()["passed"], 1)

	def test_parity_and_objective(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			root = Path(tmpdir)
			source_root = root / "src"
			source_root.mkdir()
			(source_root / "sample.cpp").write_text(
				"void helper();\n"
				"void CTrain::ProcessControl()\n"
				"{\n"
				"\thelper();\n"
				"\tif( true )\n"
				"\t{\n"
				"\t\thelper();\n"
				"\t}\n"
				"}\n",
				encoding="utf-8",
			)
			hooks_csv = root / "hooks.csv"
			hooks_csv.write_text(
				"class,fn_name,address,reversed,locked,is_virtual\n"
				"CTrain,ProcessControl,0x6f86a0,1,0,0\n",
				encoding="utf-8",
			)
			fixtures_dir = root / "fixtures"
			fixtures_dir.mkdir()
			(fixtures_dir / "backend.json").write_text(json.dumps({
				"functions": {
					"0x6f86a0": {
						"address": "0x6f86a0",
						"name": "CTrain::ProcessControl",
						"signature": "void CTrain::ProcessControl()",
						"decompiled": "void CTrain::ProcessControl(){ helper(); if(flag){ helper(); } }",
						"raw_output": "void CTrain::ProcessControl(){ helper(); if(flag){ helper(); } }",
						"callers": 3,
						"callees": 2,
						"asm": {
							"address": "0x6f86a0",
							"instructions": "bl helper\ncmpwi r3,0\nbeq lbl\nbl helper\n",
							"instruction_count": 25,
							"call_count": 2,
							"has_fp_sensitive": False,
						},
					},
				},
				"remaining": [],
			}, indent=2), encoding="utf-8")
			config_path = root / "re-agent.yaml"
			config_path.write_text(json.dumps({
				"project_profile": {
					"source_root": str(source_root),
					"hooks_csv": str(hooks_csv),
				},
				"backend": {
					"type": "stub",
					"fixtures_dir": str(fixtures_dir),
				},
			}), encoding="utf-8")
			config = load_config(config_path)
			hooks = read_hooks(hooks_csv)
			from re_agent.backend.registry import create_backend
			backend = create_backend(config.backend)
			results = run_parity(hooks, source_root, config, backend=backend)
			self.assertEqual(len(results), 1)
			self.assertEqual(results[0]["status"].value, "green")
			objective = verify_candidate(
				"void CTrain::ProcessControl(){ helper(); if(flag){ helper(); } }",
				FunctionTarget(address="0x6f86a0", class_name="CTrain", function_name="ProcessControl"),
				backend,
			)
			self.assertEqual(objective.verdict.value, "PASS")


if __name__ == "__main__":
	unittest.main()
