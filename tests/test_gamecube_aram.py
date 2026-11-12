"""Host tests for the GameCube ARAM allocator and asset cache."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GC = ROOT / "engine/platform/gamecube"


class GameCubeAramTests(unittest.TestCase):
    def test_allocator_and_cache(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "aram"
            subprocess.run(
                [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-I", str(GC),
                 str(GC / "aram_core.c"), str(ROOT / "tests/fixtures/gamecube_aram_test.c"),
                 "-o", str(binary)],
                check=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("aram ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
