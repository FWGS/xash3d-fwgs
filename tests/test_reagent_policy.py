from __future__ import annotations

import unittest
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/agent"))

from gc_reagent_policy import accept_report, target_eligibility  # noqa: E402


class ReagentPolicyTests(unittest.TestCase):
	def test_delta_filesystem_failure_is_not_a_reagent_target(self) -> None:
		decision = target_eligibility(
			"find found maps/c0a0e.bsp\n"
			"Delta_InitFields: couldn't load file delta.lst\n"
		)
		self.assertFalse(decision["eligible"])
		self.assertEqual(decision["reason"], "filesystem_contract_without_function_target")
		self.assertIn("FS_FileExists", decision["next_action"])

	def test_function_address_evidence_allows_reagent(self) -> None:
		decision = target_eligibility(
			"Function returns unexpected result at 0x80012340 after guest failure trace"
		)
		self.assertTrue(decision["eligible"])
		self.assertIn("0x80012340", decision["evidence"])

	def test_reagent_report_cannot_replace_runtime_gates(self) -> None:
		report = {
			"address": "0x80012340",
			"decompile": "int suspicious(void) { return 1; }",
			"source_match": "engine/common/foo.c:42",
			"confidence": 0.92,
		}
		accepted, failures = accept_report(report, build_ok=True, dolphin_ok=False)
		self.assertFalse(accepted)
		self.assertIn("normal Dolphin gate failed", failures)

	def test_report_requires_all_function_fields(self) -> None:
		accepted, failures = accept_report({"address": "0x80012340"}, build_ok=True, dolphin_ok=True)
		self.assertFalse(accepted)
		self.assertEqual(len(failures), 3)


if __name__ == "__main__":
	unittest.main()
