"""The ARAM prefetch manifest lists DoD grenade audio, and only for DoD data."""
from pathlib import Path
import importlib.util
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load_disc_builder():
    spec = importlib.util.spec_from_file_location("build_gamecube_disc", ROOT / "scripts/build-gamecube-disc.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class DodPrefetchManifestTests(unittest.TestCase):
    def test_grenade_audio_only_for_dod_data(self):
        builder = load_disc_builder()
        with tempfile.TemporaryDirectory() as src_dir, tempfile.TemporaryDirectory() as out_dir:
            source, output = Path(src_dir), Path(out_dir)
            (source / "sound/weapons").mkdir(parents=True)
            (source / "sound/weapons/explode3.wav").write_bytes(b"x" * 100)
            (source / "sound/weapons/grenthrow.wav").write_bytes(b"x" * 50)

            text = builder.build_c0a0_prefetch_manifest(source, output).read_text()
            self.assertNotIn("explode3", text, "HL1 data must keep its manifest")

            (source / "models").mkdir()
            (source / "models/v_garand.mdl").write_bytes(b"x")
            text = builder.build_c0a0_prefetch_manifest(source, output).read_text()
            self.assertIn("sound/weapons/explode3.wav 100\n", text)
            self.assertIn("sound/weapons/grenthrow.wav 50\n", text)
            self.assertNotIn("grenpinpull", text, "absent files are never listed")

            (source / "sprites").mkdir()
            (source / "sprites/320_train.spr").write_bytes(b"x" * 10)
            text = builder.build_c0a0_prefetch_manifest(source, output).read_text()
            self.assertNotIn("320_train", text, "the HL1 c0a0 name scan is skipped for DoD data")

    def test_dod_manifest_is_ranked_and_capped(self):
        builder = load_disc_builder()
        with tempfile.TemporaryDirectory() as src_dir, tempfile.TemporaryDirectory() as out_dir:
            source, output = Path(src_dir), Path(out_dir)
            (source / "models").mkdir()
            (source / "models/v_garand.mdl").write_bytes(b"x")
            weapons = source / "sound/weapons"
            weapons.mkdir(parents=True)
            for index in range(40):
                (weapons / f"gun{index:02d}_shoot.wav").write_bytes(b"x" * 10)
            for name in ("explode3.wav", "grenthrow.wav", "colt_reload_clipin.wav", "knife_hit1.wav"):
                (weapons / name).write_bytes(b"x" * 10)
            lines = builder.build_c0a0_prefetch_manifest(source, output).read_text().splitlines()[1:]
            self.assertEqual(len(lines), builder.GC_PREFETCH_MAX_ENTRIES)
            self.assertTrue(lines[0].startswith("sound/weapons/explode3.wav"))
            self.assertTrue(lines[1].startswith("sound/weapons/grenthrow.wav"))
            self.assertTrue(lines[2].startswith("sound/weapons/gun00_shoot.wav"), "shots rank before reloads")
            self.assertNotIn("knife_hit1", "".join(lines), "lowest rank is dropped by the cap")

    def test_dod_manifest_skips_oversized_files(self):
        builder = load_disc_builder()
        with tempfile.TemporaryDirectory() as src_dir, tempfile.TemporaryDirectory() as out_dir:
            source, output = Path(src_dir), Path(out_dir)
            (source / "models").mkdir()
            (source / "models/v_garand.mdl").write_bytes(b"x")
            weapons = source / "sound/weapons"
            weapons.mkdir(parents=True)
            (weapons / "explode3.wav").write_bytes(b"x" * builder.GC_PREFETCH_DOD_MAX_FILE)
            (weapons / "explode4.wav").write_bytes(b"x" * (builder.GC_PREFETCH_DOD_MAX_FILE + 1))
            (weapons / "grenthrow.wav").write_bytes(b"x" * 5)
            lines = builder.build_c0a0_prefetch_manifest(source, output).read_text().splitlines()[1:]
            names = [line.split()[0] for line in lines]
            self.assertIn("sound/weapons/explode3.wav", names)
            self.assertNotIn("sound/weapons/explode4.wav", names, "a file over the per-file cap is skipped")
            self.assertIn("sound/weapons/grenthrow.wav", names, "later entries that still fit are kept")


if __name__ == "__main__":
    unittest.main()
