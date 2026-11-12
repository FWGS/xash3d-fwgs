"""Host tests for the GameCube ARAM skin streaming policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GC = ROOT / "engine/platform/gamecube"


class GameCubeSkinStreamTests(unittest.TestCase):
    def test_skin_stream(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "skin_stream"
            subprocess.run(
                [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-I", str(GC),
                 str(GC / "aram_core.c"), str(GC / "skin_stream.c"),
                 str(ROOT / "tests/fixtures/gamecube_skin_stream_test.c"),
                 "-o", str(binary)],
                check=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("skin stream ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
