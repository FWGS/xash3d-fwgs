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


if __name__ == "__main__":
    unittest.main()
