from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path


def load_script_module(name: str, path: Path):
	spec = importlib.util.spec_from_file_location(name, path)
	module = importlib.util.module_from_spec(spec)
	assert spec is not None and spec.loader is not None
	sys.modules[name] = module
	spec.loader.exec_module(module)
	return module


class AiAutonomyTests(unittest.TestCase):
	def test_auto_discovery_prefers_recent_blocker(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			root = Path(tmpdir)
			(root / ".ai/goals").mkdir(parents=True)
			(root / ".ai/state").mkdir(parents=True)
			(root / ".ai/prompts").mkdir(parents=True)
			(root / "engine/common").mkdir(parents=True)
			(root / "engine/platform/gamecube").mkdir(parents=True)
			(root / "ref/gx").mkdir(parents=True)
			(root / ".ai/goals/GAMECUBE_PORT_GOALS.md").write_text(
				"## G36 [ ] improve frame budget\n- Status: ACTIVE\n",
				encoding="utf-8",
			)
			(root / ".ai/state/goal-loop-memory.json").write_text(json.dumps({
				"conact": {
					"recent_step_record": [
						{
							"goal": "G36",
							"phase": "dolphin-probe",
							"exit_code": 1,
							"observation": "_Mem_Alloc: out of memory in mod_bmodel",
							"intent": "Reduce current route memory pressure",
							"result": "memory_pressure",
						}
					]
				}
			}), encoding="utf-8")
			(root / ".ai/state/dolphin-harness-latest.md").write_text(
				"Latest route hit a guest memory failure after map load.",
				encoding="utf-8",
			)
			for prompt in (
				".ai/prompts/GAMECUBE_LOCAL_MISSION.md",
				".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
				".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
				".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",
			):
				(root / prompt).write_text("context", encoding="utf-8")
			for path in (
				"engine/common/zone.c",
				"engine/common/mod_bmodel.c",
				"engine/common/mod_studio.c",
				"engine/platform/gamecube/vid_gamecube.c",
				"ref/gx/r_surf.c",
			):
				(root / path).write_text("/* source */\n", encoding="utf-8")

			module = load_script_module(
				"ai_auto_discover",
				Path(__file__).resolve().parents[1] / "scripts/ai-auto-discover.py",
			)
			items = module.discover_items(root)
			self.assertTrue(items)
			self.assertEqual(items[0].kind, "discovery")
			self.assertEqual(items[0].failure_class, "memory_pressure")
			self.assertIn("G36", items[0].task)
			self.assertIn("editable files already in chat", items[0].task)

	def test_low_vram_budget_caps_context_and_history(self) -> None:
		module = load_script_module("aider_token_budget", Path(__file__).resolve().parents[1] / "scripts/aider-token-budget.py")
		old_profile = os.environ.get("AI_LOCAL_PROFILE")
		old_flag = os.environ.get("AIDER_LOW_VRAM_PROFILE")
		try:
			os.environ["AI_LOCAL_PROFILE"] = "rtx-pro-4000-24gb"
			os.environ["AIDER_LOW_VRAM_PROFILE"] = "1"
			budgets = module.compute_budgets(65536, 1)
		finally:
			if old_profile is None:
				os.environ.pop("AI_LOCAL_PROFILE", None)
			else:
				os.environ["AI_LOCAL_PROFILE"] = old_profile
			if old_flag is None:
				os.environ.pop("AIDER_LOW_VRAM_PROFILE", None)
			else:
				os.environ["AIDER_LOW_VRAM_PROFILE"] = old_flag
		self.assertEqual(budgets["AIDER_MODEL_MAX_CONTEXT"], 32768)
		self.assertLessEqual(budgets["AIDER_MAX_CHAT_HISTORY_TOKENS"], 256)
		self.assertLessEqual(budgets["AIDER_OUTPUT_TOKENS_INITIAL"], 768)

	def test_discovery_supervisor_state_overrides_runtime_probe_retry(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			root = Path(tmpdir)
			(root / ".ai/goals").mkdir(parents=True)
			(root / ".ai/state").mkdir(parents=True)
			(root / ".ai/prompts").mkdir(parents=True)
			(root / "scripts").mkdir(parents=True)
			(root / ".ai/goals/GAMECUBE_PORT_GOALS.md").write_text(
				"## G72 [ ] close runtime blocker\n- Status: ACTIVE\n",
				encoding="utf-8",
			)
			(root / ".ai/state/discovery-supervisor.json").write_text(json.dumps({
				"result": "review_reject",
				"intent": "Align the discovery path with the acceptance gates before retrying source edits.",
				"observation": "A source-only discovery commit built successfully but the review gate rejected it.",
			}), encoding="utf-8")
			for prompt in (
				".ai/prompts/GAMECUBE_LOCAL_MISSION.md",
				".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
				".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
			):
				(root / prompt).write_text("context", encoding="utf-8")
			for path in (
				"scripts/ai-review.sh",
				"scripts/ai-run-until-done.py",
				"scripts/ai-aider-pass.sh",
				"scripts/ai-auto-discover.py",
			):
				(root / path).write_text("# stub\n", encoding="utf-8")

			module = load_script_module(
				"ai_auto_discover_review_reject",
				Path(__file__).resolve().parents[1] / "scripts/ai-auto-discover.py",
			)
			items = module.discover_items(root)
			self.assertTrue(items)
			self.assertEqual(items[0].kind, "discovery")
			self.assertEqual(items[0].failure_class, "review_reject")
			self.assertIn("acceptance gates", items[0].task)

	def test_repeated_no_edit_with_harness_evidence_returns_to_runtime_probe(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			root = Path(tmpdir)
			(root / ".ai/goals").mkdir(parents=True)
			(root / ".ai/state").mkdir(parents=True)
			(root / ".ai/prompts").mkdir(parents=True)
			(root / "engine/platform/gamecube").mkdir(parents=True)
			(root / "scripts").mkdir(parents=True)
			(root / ".ai/goals/GAMECUBE_PORT_GOALS.md").write_text(
				"## G72 [ ] close runtime blocker\n- Status: ACTIVE\n",
				encoding="utf-8",
			)
			(root / ".ai/state/dolphin-harness-latest.md").write_text(
				"Status: host_failure\nmap load marker missing\n",
				encoding="utf-8",
			)
			(root / ".ai/state/discovery-supervisor.json").write_text(json.dumps({
				"result": "no_edit",
				"repeat_count": 2,
				"intent": "Tighten the automation path before retrying the runtime fix.",
				"observation": "Discovery pass exited 10 before an accepted patch.",
			}), encoding="utf-8")
			(root / ".ai/README.md").write_text("context", encoding="utf-8")
			for prompt in (
				".ai/prompts/GAMECUBE_LOCAL_MISSION.md",
				".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
				".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
			):
				(root / prompt).write_text("context", encoding="utf-8")
			(root / "engine/platform/gamecube/sys_gamecube.c").write_text("/* gc */\n", encoding="utf-8")

			module = load_script_module(
				"ai_auto_discover_model_budget",
				Path(__file__).resolve().parents[1] / "scripts/ai-auto-discover.py",
			)
			items = module.discover_items(root)
			self.assertTrue(items)
			self.assertEqual(items[0].kind, "discovery")
			self.assertEqual(items[0].failure_class, "runtime_probe")
			self.assertEqual(items[0].context, ["engine/platform/gamecube/sys_gamecube.c"])
			self.assertIn("return to fresh runtime evidence", items[0].task)

	def test_dirty_runtime_tree_forces_runtime_probe_over_automation_repair(self) -> None:
		with tempfile.TemporaryDirectory() as tmpdir:
			root = Path(tmpdir)
			subprocess = __import__("subprocess")
			subprocess.run(["git", "init", "-q"], cwd=root, check=True)
			subprocess.run(["git", "config", "user.email", "test@example.com"], cwd=root, check=True)
			subprocess.run(["git", "config", "user.name", "test"], cwd=root, check=True)

			(root / ".ai/goals").mkdir(parents=True)
			(root / ".ai/state").mkdir(parents=True)
			(root / ".ai/prompts").mkdir(parents=True)
			(root / "engine/common").mkdir(parents=True)
			(root / "engine/platform/gamecube").mkdir(parents=True)
			(root / "engine/common/host.c").write_text("/* host */\n", encoding="utf-8")
			(root / "engine/common/model.c").write_text("/* model */\n", encoding="utf-8")
			(root / "engine/client").mkdir(parents=True)
			(root / "engine/client/cl_main.c").write_text("/* cl */\n", encoding="utf-8")
			(root / "engine/server").mkdir(parents=True)
			(root / "engine/server/sv_init.c").write_text("/* sv */\n", encoding="utf-8")
			(root / "engine/platform/gamecube/sys_gamecube.c").write_text("/* gc */\n", encoding="utf-8")
			(root / ".ai/goals/GAMECUBE_PORT_GOALS.md").write_text(
				"## G72 [ ] close runtime blocker\n- Status: ACTIVE\n",
				encoding="utf-8",
			)
			(root / ".ai/state/discovery-supervisor.json").write_text(json.dumps({
				"result": "model_budget",
				"repeat_count": 1,
				"intent": "Reduce context and output pressure before retrying the runtime fix.",
				"observation": "Discovery pass exited 18 before an accepted patch.",
			}), encoding="utf-8")
			for prompt in (
				".ai/prompts/GAMECUBE_LOCAL_MISSION.md",
				".ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md",
				".ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md",
				".ai/prompts/GAMECUBE_MEMORY_BUDGET.md",
			):
				(root / prompt).write_text("context", encoding="utf-8")
			subprocess.run(["git", "add", "."], cwd=root, check=True)
			subprocess.run(["git", "commit", "-qm", "chore: seed"], cwd=root, check=True)
			(root / "engine/platform/gamecube/sys_gamecube.c").write_text("/* dirty gc */\n", encoding="utf-8")

			module = load_script_module(
				"ai_auto_discover_dirty_runtime",
				Path(__file__).resolve().parents[1] / "scripts/ai-auto-discover.py",
			)
			items = module.discover_items(root)
			self.assertTrue(items)
			self.assertEqual(items[0].kind, "discovery")
			self.assertEqual(items[0].failure_class, "runtime_probe")
			self.assertIn("Dirty engine or runtime files already exist", items[0].task)


if __name__ == "__main__":
	unittest.main()
