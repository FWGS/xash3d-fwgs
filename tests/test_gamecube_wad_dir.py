"""Host tests for the GameCube ARAM-resident WAD lump directory."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameCubeWadDirTests(unittest.TestCase):
    def test_wad_dir(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "wad_dir"
            subprocess.run(
                [compiler, "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-I", str(ROOT / "filesystem"),
                 str(ROOT / "filesystem/wad_dir_gc.c"),
                 str(ROOT / "tests/fixtures/gamecube_wad_dir_test.c"),
                 "-o", str(binary)],
                check=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("wad dir ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
