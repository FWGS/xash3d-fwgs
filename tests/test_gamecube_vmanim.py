"""Host tests for mapping HL1 weapon animations onto DoD viewmodels."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CLIENT = ROOT / "engine/client"


class GameCubeViewmodelAnimTests(unittest.TestCase):
    def test_hl_anims_map_to_named_sequences(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "vmanim"
            subprocess.run(
                [compiler, "-std=c99", "-Wall", "-Wextra", "-Wno-missing-field-initializers", "-Werror",
                 "-fsanitize=address,undefined", "-I", str(CLIENT),
                 str(CLIENT / "gc_vmanim.c"), str(ROOT / "tests/fixtures/gamecube_vmanim_test.c"),
                 "-o", str(binary)],
                check=True,
            )
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("vmanim ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
