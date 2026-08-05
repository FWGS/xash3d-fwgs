from __future__ import annotations

import importlib.util
import json
import os
import struct
import sys
import tempfile
import unittest
import wave
from unittest.mock import patch
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

	def test_native_gcvid_converter_preserves_avi_fallback_and_encoder(self) -> None:
		converter = (ROOT / "scripts/convert-avi-to-gcvid.py").read_text(encoding="utf-8")
		disc = (ROOT / "scripts/build-gamecube-disc.py").read_text(encoding="utf-8")
		self.assertIn("build_gcvid_companion", converter)
		self.assertIn("build_gcpcm_companion", converter)
		self.assertIn("rgb565=True", converter)
		self.assertIn("stage_intro_avi_data", disc)
		self.assertIn("SMOKE_INTRO_MEDIA", disc)
		self.assertIn("build_intro_gcvid_companions", disc)
		self.assertIn('GCPCM_MAGIC = b"GCPA"', disc)
		self.assertIn('GCPCM_RATE = 48000', disc)
		self.assertIn("GameCube big-endian byte order", disc)

	def test_native_audio_runtime_requires_mixer_format_and_keeps_fallback(self) -> None:
		runtime = (ROOT / "engine/client/avi/avi_gc.c").read_text(encoding="utf-8")
		self.assertIn('".gcpcm"', runtime)
		self.assertIn("AVI_AttachNativeAudio", runtime)
		self.assertIn("SOUND_DMA_SPEED", runtime)
		self.assertIn("native_audio_file", runtime)
		self.assertIn("AVI_AttachAudioFromAVI", runtime)

	def test_dolphin_classification_accepts_native_audio_marker(self) -> None:
		harness = load_script("vision_markers", "scripts/dolphin-vision-test.py")
		result = harness.classify_logs(
			"Xash3D GameCube: bootstrap\n"
			"Xash3D GameCube: native intro audio opened media/valve.gcpcm rate=48000 width=2 channels=2 bytes=1920000\n"
			"Xash3D GameCube: native intro audio queued bytes=8192 rate=48000\n",
			"c0a0e",
		)
		self.assertTrue(result["markers"]["native_audio_opened"])
		self.assertTrue(result["markers"]["native_audio_queued"])
		self.assertEqual(result["audio"], "intro_pcm_submitted")

	def test_audio_comparator_aligns_and_measures_fixture(self) -> None:
		compare = load_script("audio_compare", "scripts/compare-audio-fixture.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			root = Path(tmpdir)
			frames = bytearray()
			for i in range(4800):
				sample = int(12000 * ((i % 80) / 40.0 - 1.0))
				frames.extend(struct.pack("<hh", sample, sample))
			ref_path = root / "ref.wav"
			candidate_path = root / "candidate.wav"
			for path, payload in ((ref_path, bytes(frames)), (candidate_path, b"\0\0\0\0" * 37 + bytes(frames))):
				with wave.open(str(path), "wb") as wav:
					wav.setnchannels(2)
					wav.setsampwidth(2)
					wav.setframerate(48000)
					wav.writeframes(payload)
			reference, rate = compare.read_wav(ref_path)
			candidate, candidate_rate = compare.read_wav(candidate_path)
			result = compare.metrics(reference, compare.resample(candidate, candidate_rate, rate), rate)
			self.assertEqual(result["offset_samples"], 37)
			self.assertEqual(result["aligned_frames"], len(reference))
			self.assertEqual(result["channel_mismatch_rms"], 0.0)
			self.assertGreater(result["correlation"], 0.99)

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

	def test_video_gate_requires_complete_paced_audio_video(self) -> None:
		gate = load_script("video_playback_gate", "scripts/gamecube-video-playback-gate.py")
		text = "\n".join((
			"intro GCVID opened media/valve.gcvid (320x240, 150 frames, static hold, rgb565)",
			"intro AVI audio PCM rate=22050 width=1 channels=1",
			"intro AVI audio attached media/valve.avi rate=22050 width=1 channels=1 chunks=10",
			"intro AVI audio start synced to first uploaded frame",
			"intro AVI decoded first frame",
			"audio submitted nonzero PCM chunks=1 peak=256",
			"00:01:000 Core: intro AVI progress frame=15/150 elapsed=1.01",
			"00:02:000 Core: intro AVI progress frame=30/150 elapsed=2.01",
			"00:04:000 Core: intro AVI progress frame=60/150 elapsed=4.00",
			"00:08:000 Core: intro AVI progress frame=120/150 elapsed=8.01",
			"intro AVI reached end frame=150/150 elapsed=10.00",
		))
		self.assertTrue(gate.check(text)[0])
		self.assertFalse(gate.check(text.replace("frame=120/150", "frame=90/150"))[0])
		slow = text.replace("00:02:000", "00:06:300")
		slow_ok, slow_failures = gate.check(slow)
		self.assertFalse(slow_ok)
		self.assertTrue(any("Dolphin host pacing" in item for item in slow_failures))

	def test_cinematic_gpu_path_preserves_dirty_tile_contract(self) -> None:
		api = (ROOT / "engine/ref_api.h").read_text(encoding="utf-8")
		avi = (ROOT / "engine/client/avi/avi_gc.c").read_text(encoding="utf-8")
		gx = (ROOT / "ref/gx/r_gx_world.c").read_text(encoding="utf-8")
		self.assertIn("GL_UpdateCinematicTexture", api)
		self.assertIn("raw_dirty_tiles", avi)
		self.assertIn("raw_dirty_count", avi)
		self.assertIn("R_GXUpdateRawCinematicTiles", gx)
		self.assertIn("GX_InvalidateTexAll", gx)

	def test_dolphin_harness_exposes_cpu_and_backend_experiments(self) -> None:
		harness = load_script("dolphin_vision_config", "scripts/dolphin-vision-test.py")
		with tempfile.TemporaryDirectory() as tmpdir, patch.dict(
			os.environ, {
				"DOLPHIN_CPU_THREAD": "1",
				"DOLPHIN_CPU_CORE": "1",
				"DOLPHIN_GFX_BACKEND": "Vulkan",
				"DOLPHIN_GFX_MULTITHREADING": "1",
			}, clear=False
		):
			harness.write_config(Path(tmpdir))
			config = (Path(tmpdir) / "Config" / "Dolphin.ini").read_text(encoding="utf-8")
			self.assertIn("CPUThread = True", config)
			self.assertIn("CPUCore = 1", config)
			self.assertIn("GFXBackend = Vulkan", config)
			self.assertIn("BackendMultithreading = True", (Path(tmpdir) / "Config" / "GFX.ini").read_text(encoding="utf-8"))

	def test_gameplay_gate_requires_ordered_post_action_stability(self) -> None:
		gate = load_script("gameplay_gate_order", "scripts/gamecube-gameplay-gate.py")
		base = "\n".join((
			"Xash3D GameCube: map loaded c0a0",
			"Xash3D GameCube: entity lump spawn ready",
			"Xash3D GameCube: probe gameplay action attack",
			"Xash3D GameCube: probe gameplay action jump",
			"Xash3D GameCube: probe jump PMove ready velocity=(0,0,180) flags=0",
			"Xash3D GameCube: probe gameplay action use",
			"Xash3D GameCube: probe gameplay input ready",
			"Xash3D GameCube: probe native move/look begin",
			"Xash3D GameCube: native axis usercmd ready delta=(1,0,0)",
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

	def test_g508_config_roundtrip_probe_contract(self) -> None:
		probe = (ROOT / "filesystem/probe_save_gc.c").read_text(encoding="utf-8")
		header = (ROOT / "filesystem/probe_save_gc.h").read_text(encoding="utf-8")
		sys_gc = (ROOT / "engine/platform/gamecube/sys_gamecube.c").read_text(encoding="utf-8")
		vid = (ROOT / "engine/platform/gamecube/vid_gamecube.c").read_text(encoding="utf-8")
		io = (ROOT / "filesystem/io.c").read_text(encoding="utf-8")
		disc = (ROOT / "scripts/build-gamecube-disc.py").read_text(encoding="utf-8")
		boot = (ROOT / "scripts/dolphin-boot-probe.sh").read_text(encoding="utf-8")
		packet = (ROOT / "scripts/gamecube-release-packet.py").read_text(encoding="utf-8")

		self.assertIn("-gcconfigroundtrip", probe)
		self.assertIn("GC_ProbeSaveRename", header)
		self.assertIn("GC_ProbeSaveDelete", header)
		self.assertIn("GC_ProbeSaveRename", io)
		self.assertIn("GC_ProbeSaveDelete", io)
		self.assertIn("configroundtrip", sys_gc)
		self.assertIn("G508 config round trip ready", vid)
		self.assertIn("--probe-configroundtrip", disc)
		self.assertIn("DOLPHIN_G508", boot)
		self.assertIn("G508 config round trip ready", packet)
		self.assertIn("persist_ok", packet)
		self.assertIn("changelevel_ok", packet)

	def test_g509_changelevel_soak_dry_run(self) -> None:
		soak = load_script("soak_probe", "scripts/gamecube-soak-probe.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			log_dir = Path(tmpdir) / "soak"
			code = soak.main([
				"--repo", str(ROOT),
				"--g509",
				"--dry-run",
				"--iterations", "2",
				"--log-dir", str(log_dir),
			])
			self.assertEqual(code, 0)
			report = json.loads((log_dir / "report.json").read_text(encoding="utf-8"))
			self.assertTrue(report["ok"])
			self.assertEqual(report["mode"], "changelevel")
			self.assertEqual(report["changelevel_route"], "c0a0:c0a0a")
			self.assertTrue(report["require_changelevel"])
			self.assertEqual(len(report["results"]), 2)
			self.assertEqual(report["results"][0]["changelevel_to"], "c0a0a")
			self.assertTrue(report["results"][0]["landmark_restore"])
			summary = (log_dir / "summary.md").read_text(encoding="utf-8")
			self.assertIn("Changelevel route: `c0a0:c0a0a`", summary)

	def test_g509_soak_parser_accepts_changelevel_ready(self) -> None:
		soak = load_script("soak_parse", "scripts/gamecube-soak-probe.py")
		output = (
			"CHANGELEVEL_READY: Destination map, landmark state, and required runtime continuity markers passed.\n"
			"Xash3D GameCube: G68 changelevel ready from=c0a0 to=c0a0a\n"
			"Xash3D GameCube: MAP_READY c0a0a\n"
			"Xash3D GameCube: G100 landmark restore health=77 armor=50\n"
			"mem stage=changelevel total=4.00 Mb hwm=5.00 Mb\n"
			"FRAME_BUDGET_STATS: samples=12 avg=1.00ms p95=1.10ms max=2.00ms\n"
			"Logs: .ai/logs/example\n"
		)
		item = soak.parse_iteration(
			ROOT,
			1,
			"c0a0",
			mode="changelevel",
			changelevel_to="c0a0a",
			exit_code=0,
			elapsed=1.5,
			output=output,
		)
		self.assertEqual(item.status, "PASS")
		self.assertTrue(item.landmark_restore)
		self.assertEqual(item.hwm_bytes, 5 * 1024 * 1024)
		self.assertEqual(item.frame_samples, 12)
		ok, note = soak.classify([item, item], 256 * 1024, require_changelevel=True)
		self.assertTrue(ok)
		self.assertIn("passed", note)

	def test_ogc_stack_prefers_libogc2_for_swiss(self) -> None:
		stack = load_script("ogc_stack", "scripts/waifulib/gamecube_ogc_stack.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			dkp = Path(tmpdir)
			ogc2 = dkp / "libogc2" / "gamecube"
			(ogc2 / "include").mkdir(parents=True)
			(ogc2 / "lib").mkdir(parents=True)
			(ogc2 / "lib" / "libogc.a").write_bytes(b"ogc")
			(ogc2 / "lib" / "libfat.a").write_bytes(b"fat")
			(ogc2 / "lib" / "libdvm.a").write_bytes(b"dvm")
			info = stack.resolve_ogc_stack(str(dkp), environ={"XASH_GAMECUBE_OGC_STACK": "auto"})
			self.assertTrue(info["available"])
			self.assertEqual(info["stack"], "libogc2")
			self.assertEqual(info["fat_provider"], "libdvm")
			self.assertIn("-DXASH_GAMECUBE_LIBOGC2=1", info["cflags_defines"])
			self.assertIn("-DXASH_GAMECUBE_LIBDVM=1", info["cflags_defines"])
			self.assertIn("-mogc", info["linkflags"])
			self.assertIn("-L%s" % (ogc2 / "lib"), info["linkflags"])
			flags = stack.engine_extra_ldflags(info)
			self.assertIn("-lfat", flags)
			self.assertNotIn("-ldvm", flags)
			self.assertIn("-lasnd", flags)
			self.assertIn("-liso9660", flags)

	def test_ogc_stack_falls_back_to_classic_libogc(self) -> None:
		stack = load_script("ogc_stack_classic", "scripts/waifulib/gamecube_ogc_stack.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			dkp = Path(tmpdir)
			classic = dkp / "libogc"
			(classic / "include").mkdir(parents=True)
			(classic / "lib" / "cube").mkdir(parents=True)
			(classic / "share").mkdir(parents=True)
			(classic / "lib" / "cube" / "libogc.a").write_bytes(b"ogc")
			(classic / "lib" / "cube" / "libfat.a").write_bytes(b"fat")
			(classic / "share" / "ogc.specs").write_text("SPEC\n", encoding="utf-8")
			info = stack.resolve_ogc_stack(str(dkp), environ={"XASH_GAMECUBE_OGC_STACK": "auto"})
			self.assertTrue(info["available"])
			self.assertEqual(info["stack"], "libogc")
			self.assertEqual(info["fat_provider"], "libfat")
			self.assertTrue(any(flag.startswith("-specs=") for flag in info["linkflags"]))

	def test_ogc_stack_swiss_contract_in_tree(self) -> None:
		sys_gc = (ROOT / "engine/platform/gamecube/sys_gamecube.c").read_text(encoding="utf-8")
		build = (ROOT / "scripts/build-gamecube.sh").read_text(encoding="utf-8")
		verify = (ROOT / "scripts/ai-verify.sh").read_text(encoding="utf-8")
		readme = (ROOT / "README.md").read_text(encoding="utf-8")
		docs = (ROOT / "docs/GAMECUBE_BUILDING_GAMECUBE.md").read_text(encoding="utf-8")
		xcompile = (ROOT / "scripts/waifulib/xcompile.py").read_text(encoding="utf-8")
		self.assertIn("OGC stack=libogc2", sys_gc)
		self.assertIn("XASH_GAMECUBE_OGC_STACK", build)
		self.assertIn("gamecube_ogc_stack.py", verify)
		self.assertIn("libogc2", readme)
		self.assertIn("libdvm", docs)
		self.assertIn("resolve_ogc_stack", xcompile)
		self.assertIn("loader=swiss", build)

	def test_swiss_libdvm_volume_probe_contract(self) -> None:
		sys_gc = (ROOT / "engine/platform/gamecube/sys_gamecube.c").read_text(encoding="utf-8")
		storage_h = (ROOT / "engine/platform/gamecube/storage_gamecube.h").read_text(encoding="utf-8")
		handoff = (ROOT / "scripts/gamecube-hardware-handoff.sh").read_text(encoding="utf-8")
		matrix = (ROOT / "docs/GAMECUBE_HARDWARE_MATRIX.md").read_text(encoding="utf-8")
		launcher = (ROOT / "engine/common/launcher.c").read_text(encoding="utf-8")

		self.assertIn("carda:/", sys_gc)
		self.assertIn("cardb:/", sys_gc)
		self.assertIn("GCube_ProbeFatVolumes", sys_gc)
		self.assertIn("FAT preferred volume", sys_gc)
		self.assertIn("fatDeinit", sys_gc)
		self.assertIn("using loader argv", sys_gc)
		self.assertIn("return to Swiss loader", sys_gc)
		self.assertIn("carda:/", storage_h)
		self.assertIn("SD2SP2", handoff)
		self.assertIn("carda:/xash3d/valve/", handoff)
		self.assertIn("SD2SP2", matrix)
		self.assertIn("STUBHAXX", launcher)

	def test_swiss_fat_volume_prefers_valve_on_carda(self) -> None:
		storage = load_script("gc_storage", "scripts/waifulib/gamecube_storage.py")
		chosen = storage.select_fat_volume(
			["sd:/", "carda:/"],
			has_valve=["carda:/"],
		)
		self.assertEqual(chosen, "carda:/")
		chosen_sd = storage.select_fat_volume(["sd:/", "carda:/"], has_valve=[])
		self.assertEqual(chosen_sd, "sd:/")

	def test_writable_layout_paths_for_sd_and_carda(self) -> None:
		storage = load_script("gc_storage_layout", "scripts/waifulib/gamecube_storage.py")
		sd_paths = storage.writable_layout_paths("sd:/")
		carda_paths = storage.writable_layout_paths("carda:")
		self.assertIn("sd:/xash3d/valve/save", sd_paths)
		self.assertIn("carda:/xash3d/valve/save", carda_paths)

	def test_asset_manager_strips_carda_prefix(self) -> None:
		storage = load_script("gc_storage_strip", "scripts/waifulib/gamecube_storage.py")
		self.assertEqual(storage.strip_device_prefix("carda:/xash3d/valve"), "xash3d/valve")
		self.assertEqual(storage.strip_device_prefix("sd:/xash3d/valve"), "xash3d/valve")
		self.assertEqual(storage.strip_device_prefix("gcdisc:/xash3d/valve"), "xash3d/valve")

	def test_probe_classifies_fat_preferred_volume_carda(self) -> None:
		storage = load_script("gc_storage_parse", "scripts/waifulib/gamecube_storage.py")
		text = (
			"Xash3D GameCube: FAT volume ready sd:/\n"
			"Xash3D GameCube: FAT volume ready carda:/\n"
			"Xash3D GameCube: FAT preferred volume carda:/ (count=2)\n"
		)
		fat = storage.parse_fat_volume_status(text)
		self.assertTrue(fat["ok"])
		self.assertEqual(fat["preferred"], "carda:/")
		self.assertEqual(fat["volumes"], ["sd:/", "carda:/"])

	def test_probe_classifies_g508_config_roundtrip(self) -> None:
		storage = load_script("gc_storage_g508", "scripts/waifulib/gamecube_storage.py")
		text = "Xash3D GameCube: G508 config round trip ready route=gcprobe\n"
		g508 = storage.parse_g508_status(text)
		self.assertTrue(g508["ready"])
		self.assertEqual(g508["route"], "gcprobe")

	def test_release_packet_requires_g508_and_g509_changelevel_soak(self) -> None:
		packet = load_script("release_packet", "scripts/gamecube-release-packet.py")
		cont = packet.evaluate_persist_and_changelevel(
			"G508 config round trip ready route=sd\nCHANGELEVEL_READY: ok\n",
			"G68 changelevel ready from=c0a0 to=c0a0a\n",
		)
		self.assertTrue(cont["persist_ok"])
		self.assertTrue(cont["changelevel_ok"])
		self.assertFalse(packet.evaluate_persist_and_changelevel("MAP_READY", "")["persist_ok"])
		self.assertTrue(packet.evaluate_soak({
			"ok": True,
			"mode": "changelevel",
			"require_changelevel": True,
			"changelevel_route": "c0a0:c0a0a",
		}))
		self.assertFalse(packet.evaluate_soak({
			"ok": True,
			"mode": "map",
			"require_changelevel": True,
		}))

	def test_release_packet_dry_run_with_fixtures(self) -> None:
		packet = load_script("release_packet_dry", "scripts/gamecube-release-packet.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			tmp = Path(tmpdir)
			runtime = tmp / "runtime.log"
			gameplay = tmp / "gameplay.log"
			# Minimal markers to satisfy gameplay gate + runtime + continuity.
			runtime.write_text(
				"Xash3D GameCube: map loaded c0a0\n"
				"sampled_nonblack=1\n"
				"frame time=1.00ms\n"
				"G508 config round trip ready route=gcprobe\n"
				"CHANGELEVEL_READY: Destination map ready\n"
				"audio submitted nonzero PCM\n",
				encoding="utf-8",
			)
			gameplay.write_text(
				"NEWGAME_READY\n"
				"G45 controller ready port=0 type=standard\n"
				"G45 action attack\n"
				"G45 action jump\n"
				"G45 action use\n"
				"MAP_READY: c0a0\n"
				"sampled_nonblack=1\n",
				encoding="utf-8",
			)
			(tmp / "map.txt").write_text("MAP_COMPAT_PROBE: PASS\n", encoding="utf-8")
			(tmp / "memory.json").write_text(
				json.dumps({"runtime": {"samples": [{"map": "c0a0"}]}}),
				encoding="utf-8",
			)
			(tmp / "audio.log").write_text("audio submitted nonzero PCM\n", encoding="utf-8")
			(tmp / "soak.json").write_text(json.dumps({
				"ok": True,
				"mode": "changelevel",
				"require_changelevel": True,
				"changelevel_route": "c0a0:c0a0a",
			}), encoding="utf-8")
			out = tmp / "packet"
			argv = [
				"gamecube-release-packet.py",
				"--repo", str(ROOT),
				"--output", str(out),
				"--runtime-log", str(runtime),
				"--gameplay-log", str(gameplay),
				"--map-report", str(tmp / "map.txt"),
				"--memory-report", str(tmp / "memory.json"),
				"--audio-report", str(tmp / "audio.log"),
				"--soak-report", str(tmp / "soak.json"),
				"--dry-run",
			]
			with patch.object(sys, "argv", argv):
				code = packet.main()
			self.assertTrue((out / "validation.json").is_file())
			validation = json.loads((out / "validation.json").read_text(encoding="utf-8"))
			self.assertTrue(validation["dry_run"])
			self.assertTrue(validation["persist_ok"])
			self.assertTrue(validation["changelevel_ok"])
			self.assertIn(code, (0, 1))

	def test_build_docs_prefer_waf_libogc2_not_gekko_cmake(self) -> None:
		linking = (ROOT / "docs/GAMECUBE_GAME_MODULE_LINKING.md").read_text(encoding="utf-8")
		building = (ROOT / "docs/GAMECUBE_BUILDING_GAMECUBE.md").read_text(encoding="utf-8")
		audit = (ROOT / "docs/GAMECUBE_PORT_AUDIT.md").read_text(encoding="utf-8")
		verify = (ROOT / "scripts/ai-verify.sh").read_text(encoding="utf-8")
		self.assertIn("Deprecated", linking)
		self.assertIn("scripts/build-gamecube.sh", linking)
		self.assertIn("libogc2", building)
		self.assertNotIn("Null backend in use", audit)
		self.assertIn("unittest discover", verify)

	def test_hardware_layout_info_prints_carda(self) -> None:
		import subprocess
		result = subprocess.run(
			["bash", str(ROOT / "scripts/gamecube-hardware-layout-info.sh"), "--route", "carda"],
			cwd=ROOT,
			text=True,
			capture_output=True,
			check=True,
		)
		self.assertIn("carda:/xash3d/valve", result.stdout)

	def test_runtime_ladder_stops_at_first_missing_gate(self) -> None:
		ladder = load_script("runtime_ladder", "scripts/gamecube-runtime-ladder.py")
		partial = (
			"Xash3D GameCube: bootstrap\n"
			"Xash3D GameCube: engine subsystems ready\n"
			"Xash3D GameCube: DVD mount ready\n"
		)
		report = ladder.evaluate_ladder(partial)
		self.assertFalse(report["ok"])
		self.assertEqual(report["first_missing"], "delta_load")
		self.assertEqual(report["passed"], ["bootstrap", "engine_init", "filesystem_init"])

		complete = partial + (
			"Xash3D GameCube: G201 delta reinit ready\n"
			"COM_LoadLibrary server (registered)\n"
			"find found 'maps/c0a0.bsp'\n"
			"Xash3D GameCube: map loaded c0a0\n"
			"Xash3D GameCube: G45 controller ready port=0 type=standard\n"
			"sampled_nonblack=1\n"
		)
		full = ladder.evaluate_ladder(complete)
		self.assertTrue(full["ok"])
		self.assertIsNone(full["first_missing"])

		# Explicit delta failure must stop even if other text mentions delta.
		failed = partial + "Delta_InitFields: couldn't load file delta.lst\n"
		bad = ladder.evaluate_ladder(failed)
		self.assertEqual(bad["first_missing"], "delta_load")

	def test_runtime_ladder_cli_fixture(self) -> None:
		ladder = load_script("runtime_ladder_cli", "scripts/gamecube-runtime-ladder.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			log = Path(tmpdir) / "partial.log"
			out = Path(tmpdir) / "ladder.json"
			log.write_text(
				"Xash3D GameCube: bootstrap\n"
				"Xash3D GameCube: engine subsystems ready\n",
				encoding="utf-8",
			)
			code = ladder.main(["--fixture", str(log), "--json", str(out)])
			self.assertEqual(code, 1)
			report = json.loads(out.read_text(encoding="utf-8"))
			self.assertEqual(report["first_missing"], "filesystem_init")

	def test_experiment_manifest_records_tier_and_ogc_stack(self) -> None:
		manifest_mod = load_script("experiment_manifest", "scripts/gamecube-experiment-manifest.py")
		with tempfile.TemporaryDirectory() as tmpdir:
			out = Path(tmpdir) / "exp"
			code = manifest_mod.main([
				"--repo", str(ROOT),
				"--hypothesis", "host-only ladder dry-run",
				"--target-file", "scripts/gamecube-runtime-ladder.py",
				"--decision", "pending",
				"--output-dir", str(out),
				"--dry-run",
			])
			self.assertEqual(code, 0)
			data = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
			self.assertEqual(data["schema"], "xash3d-gc-experiment-manifest/v1")
			self.assertIn("tier", data)
			self.assertIn("value", data["tier"])
			self.assertIn("ogc_stack", data)
			self.assertIn("stack", data["ogc_stack"])
			self.assertEqual(data["hypothesis"], "host-only ladder dry-run")
			self.assertEqual(data["decision"], "pending")
			self.assertTrue(data["dry_run"])

	def test_probe_save_config_names(self) -> None:
		probe = load_script("probe_save", "scripts/waifulib/gamecube_probe_save.py")
		self.assertTrue(probe.probe_save_is_config_name("config.cfg"))
		self.assertTrue(probe.probe_save_is_config_name("config.cfg.new"))
		self.assertTrue(probe.probe_save_is_config_name("vfs.cfg.bak"))
		self.assertFalse(probe.probe_save_is_config_name("auto0.sav"))
		self.assertEqual(probe.probe_save_basename("gcprobe:/xash3d/valve/config.cfg"), "config.cfg")

	def test_probe_save_rejects_non_gcprobe_paths(self) -> None:
		probe = load_script("probe_save_paths", "scripts/waifulib/gamecube_probe_save.py")
		self.assertTrue(probe.probe_save_path_match("gcprobe:/xash3d/config.cfg", enabled=True))
		self.assertTrue(probe.probe_save_path_match("save/quick.sav", enabled=True))
		self.assertTrue(probe.probe_save_path_match("config.cfg.new", enabled=True))
		self.assertFalse(probe.probe_save_path_match("valve/maps/c0a0.bsp", enabled=True))
		self.assertTrue(probe.probe_save_rejects_non_gcprobe_paths("valve/maps/c0a0.bsp"))
		bank = probe.ProbeSaveBank()
		self.assertTrue(bank.config_roundtrip(b"unbindall\nhost_framerate \"0\"\n"))
		self.assertEqual(bank.read("config.cfg"), b"unbindall\nhost_framerate \"0\"\n")
		self.assertNotIn("config.cfg.new", bank.files)


if __name__ == "__main__":
	unittest.main()
