from __future__ import annotations

import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load_script(name: str, relative: str):
	module_name = f"host_test_{name}"
	spec = importlib.util.spec_from_file_location(module_name, ROOT / relative)
	assert spec is not None and spec.loader is not None
	module = importlib.util.module_from_spec(spec)
	sys.modules[module_name] = module
	spec.loader.exec_module(module)
	return module


class GameCubeHostTests(unittest.TestCase):
	def test_disc_asset_staging_requires_delta_lst(self) -> None:
		disc = load_script("disc", "scripts/build-gamecube-disc.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			root = Path(tmpdir)
			for asset in disc.CRITICAL_ASSETS:
				path = root / "valve" / asset
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_bytes(b"asset")
			self.assertEqual(disc.validate_assets(root / "valve"), [])
			(root / "valve" / "delta.lst").unlink()
			errors = disc.validate_assets(root / "valve")
			self.assertTrue(any("delta.lst" in error for error in errors))

	def test_staged_assets_preserve_required_delta_lst_path(self) -> None:
		disc = load_script("disc_staged", "scripts/build-gamecube-disc.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			staged = Path(tmpdir) / "valve"
			for asset in disc.CRITICAL_ASSETS:
				path = staged / asset
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_bytes(b"asset")
			self.assertEqual(disc.validate_staged_retail_assets(staged), [])

	def test_smoke_newgame_can_stage_fullphysics_override(self) -> None:
		disc = load_script("disc_fullphysics", "scripts/build-gamecube-disc.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			out = Path(tmpdir)
			disc.write_smoke_overrides(out, "c0a0", fullphysics=True)
			self.assertIn("fullphysics", (out / "gamecube.cfg").read_text(encoding="ascii"))

	def test_filesystem_search_path_contract_checks_directories_and_order(self) -> None:
		searchpath = (ROOT / "filesystem/searchpath.c").read_text(encoding="utf-8")
		self.assertIn("FS_SysFolderExists( dir )", searchpath)
		self.assertIn("FS_AddArchive_Fullpath( &g_directory_archive, dir, flags )", searchpath)
		find_file = searchpath.split("FS_FindFile(", 1)[1]
		self.assertIn("for( search = fs_searchpaths; search; search = search->next )", find_file)
		self.assertIn("FS_ShouldSkipSearchpath( search, name )", find_file)
		self.assertIn("FS_ClearFindMissCache();", searchpath)
		self.assertIn("FS_ClearFindHitCache();", searchpath)
		# A successful FileExists lookup must not make the next LoadFile lookup
		# return NULL based on a name-only hit-cache entry.
		self.assertNotIn("FS_FindHitCached( name )", searchpath)

	def test_file_exists_and_load_file_share_the_same_filesystem_api(self) -> None:
		engine = (ROOT / "engine/common/filesystem_engine.c").read_text(encoding="utf-8")
		load_body = engine.split("byte *FS_LoadFile(", 1)[1].split("}", 1)[0]
		# FS_FileExists is supplied by the filesystem module; the engine wrapper
		# must still call the same g_fsapi contract rather than a host-only path.
		self.assertIn("g_fsapi.LoadFile", load_body)
		self.assertIn("FS_FileExists", engine)
		self.assertIn("FS_LoadFile( DELTA_PATH, NULL, false )", (ROOT / "engine/common/net_encode.c").read_text(encoding="utf-8"))
		sv_init = (ROOT / "engine/server/sv_init.c").read_text(encoding="utf-8")
		self.assertIn('FS_FileExists( "delta.lst", false )', sv_init)
		self.assertIn('FS_FileExists( "valve/delta.lst", false )', sv_init)

	def test_dol_section_and_bss_metadata_is_big_endian(self) -> None:
		disc = load_script("disc_dol", "scripts/build-gamecube-disc.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			path = Path(tmpdir) / "boot.dol"
			data = bytearray(0x120)
			struct.pack_into(">I", data, 0x00, 0x100)
			struct.pack_into(">I", data, 0x48, 0x80003100)
			struct.pack_into(">I", data, 0x90, 0x20)
			struct.pack_into(">III", data, 0xD8, 0x80004000, 0x180, 0x80003100)
			path.write_bytes(data)
			sections, bss_address, bss_size, entry = disc.parse_dol(path)
			self.assertEqual(sections, [(0x100, 0x80003100, 0x20)])
			self.assertEqual((bss_address, bss_size, entry), (0x80004000, 0x180, 0x80003100))

	def test_module_registration_keeps_static_server_client_exports(self) -> None:
		source = (ROOT / "engine/platform/gamecube/dll_gamecube.c").read_text(encoding="utf-8")
		self.assertIn('dll_register( "server", lib_hl_gamecube_ppc_exports )', source)
		self.assertIn('dll_register( "client", lib_client_gamecube_ppc_exports )', source)
		self.assertIn("setup_gamecube_server_exports", source)
		self.assertIn("setup_gamecube_client_exports", source)
		self.assertIn("GAMECUBE_MAX_REGISTERED_DLLS", source)

	def test_probe_marker_parsing_and_failure_classification(self) -> None:
		analyze = load_script("probe_analyze", "scripts/dolphin-probe-analyze.py")
		text = "Xash3D GameCube: map loaded c0a0e\nXash3D GameCube: input polling active\n"
		self.assertEqual(analyze.detect_loaded_map(text), "c0a0e")
		self.assertEqual(analyze.classify_g36([10.0, 11.0, 12.0], 16.67, True, False, False, True, "map_ready")[0], "PASS")
		self.assertEqual(analyze.classify_g45(text, True, True, False)[0], "WEAK")
		loop = load_script("goal_loop_classify", "scripts/ai-goal-loop.py")
		self.assertEqual(loop.failure_class_for(1, [{"text": "Delta_InitFields: couldn't load file delta.lst"}], "dolphin-probe"), "runtime_probe")
		self.assertEqual(loop.failure_class_for(1, [{"text": "black screen"}], "dolphin-probe"), "visual_runtime")

	def test_gameplay_gate_rejects_input_only_evidence(self) -> None:
		gate = load_script("gameplay_gate", "scripts/gamecube-gameplay-gate.py")
		ok, failures = gate.check("Xash3D GameCube: probe gameplay action attack\n")
		self.assertFalse(ok)
		self.assertTrue(any("attack usercmd" in item for item in failures))

	def test_gameplay_gate_requires_ordered_post_action_stability(self) -> None:
		gate = load_script("gameplay_gate_order", "scripts/gamecube-gameplay-gate.py")
		base = "\n".join((
			"Xash3D GameCube: map loaded c0a0",
			"Xash3D GameCube: entity lump spawn ready",
			"Xash3D GameCube: probe gameplay move/look begin",
			"Xash3D GameCube: native axis usercmd ready delta=(1,0,0)",
			"Xash3D GameCube: probe gameplay action attack",
			"Xash3D GameCube: probe gameplay action jump",
			"Xash3D GameCube: probe jump PMove ready velocity=(0,0,180) flags=0",
			"Xash3D GameCube: probe gameplay action use",
			"Xash3D GameCube: probe gameplay input ready",
			"Xash3D GameCube: G120 attack usercmd buttons=1",
			"Xash3D GameCube: G121 PlaybackEvent deliver index=1 name=events/glock.sc",
			"Xash3D GameCube: world interaction use done classname=func_button",
			"Xash3D GameCube: gcmap smoke frames ready",
			"frame time=10ms\nframe time=11ms\nframe time=12ms",
		))
		self.assertEqual(gate.check(base)[0], True)
		self.assertEqual(gate.check(base.replace("gcmap smoke frames ready", ""))[0], False)

	def test_release_packet_validates_dol_elf_and_iso(self) -> None:
		packet = load_script("release_packet", "scripts/gamecube-release-packet.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			root = Path(tmpdir)
			(root / "OUT/bin").mkdir(parents=True)
			(root / "OUT/xash3d-gc.iso").parent.mkdir(parents=True, exist_ok=True)
			(root / "OUT/bin/xash").write_bytes(b"\x7fELF")
			dol = bytearray(0x100)
			dol[0x90:0x94] = (0x20).to_bytes(4, "big")
			(root / "OUT/bin/boot.dol").write_bytes(dol)
			iso = bytearray(0x8006)
			iso[0x8001:0x8006] = b"CD001"
			(root / "OUT/xash3d-gc.iso").write_bytes(iso)
			_, failures = packet.validate_artifacts(root)
			self.assertIn("ELF header is not a valid PowerPC executable", failures)
			self.assertNotIn("DOL header is empty", failures)
			self.assertNotIn("ISO9660 primary volume descriptor is missing", failures)

	def test_endian_sensitive_elf_to_dol_serialization(self) -> None:
		converter = load_script("elf_to_dol", "scripts/elf-to-dol.py")
		for endian in (">", "<"):
			data = bytearray(0x100)
			data[:4] = b"\x7fELF"
			data[4:6] = bytes((1, 2 if endian == ">" else 1))
			struct.pack_into(f"{endian}I", data, 0x18, 0x80003100)
			struct.pack_into(f"{endian}I", data, 0x1C, 0x40)
			struct.pack_into(f"{endian}H", data, 0x2A, 32)
			struct.pack_into(f"{endian}H", data, 0x2C, 1)
			struct.pack_into(f"{endian}IIIIII", data, 0x40, 1, 0x80, 0x80003100, 0, 0x10, 0x20)
			segments, entry = converter.parse_elf_segments(bytes(data))
			self.assertEqual(entry, 0x80003100)
			self.assertEqual(segments[0].file_size, 0x10)
			self.assertEqual(segments[0].mem_size, 0x20)
		header = converter.create_dol_header(segments, entry)
		self.assertEqual(struct.unpack_from(">I", header, 0xD8)[0], 0x80003110)
		self.assertEqual(struct.unpack_from(">I", header, 0xDC)[0], 0x10)

	def test_memory_budget_report_generation_is_evidence_only(self) -> None:
		memory = load_script("memory_evidence", "scripts/gamecube-memory-evidence.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			root = Path(tmpdir)
			(root / "OUT/bin").mkdir(parents=True)
			(root / "OUT/bin/boot.dol").write_bytes(b"\0" * 0x100)
			(root / ".ai/logs/run").mkdir(parents=True)
			(root / ".ai/logs/run/stderr.log").write_text(
				"Xash3D GameCube: mem stage=bsp total=4.00 MiB delta=1.00 MiB hwm=5.00 MiB map=c0a0\n"
				"Xash3D GameCube: mem FAIL subsystem=mod_bmodel size=128 KiB map=c0a0 at=mod.c:10 total=5.00 MiB hwm=5.00 MiB\n",
				encoding="utf-8",
			)
			report = memory.generate(root, [])
			self.assertEqual(report["runtime"]["mem1_high_water_bytes"], 5 * 1024 * 1024)
			self.assertEqual(report["runtime"]["mem2_high_water_bytes"], "UNAVAILABLE")
			self.assertEqual(report["runtime"]["per_map_peak"][0]["map"], "c0a0")
			self.assertEqual(report["runtime"]["largest_failed_allocation"]["subsystem"], "mod_bmodel")
			self.assertEqual(report["runtime"]["texture_lightmap_audio_cache"], "UNAVAILABLE without tagged telemetry")
			json.dumps(report)


if __name__ == "__main__":
	unittest.main()
