"""Check the production GameCube clock's units and 64-bit timebase handling."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GameCubeClockTests(unittest.TestCase):
    def test_timebase_returns_seconds_across_low_word_rollover(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "engine/platform/gamecube/sys_gamecube.c").read_text()
        function = re.search(r"double Platform_DoubleTime\( void \)\n\{.*?\n\}",
                             source, re.DOTALL)
        self.assertIsNotNone(function)
        fixture = r'''
#include <assert.h>
#include <stdint.h>
#define XASH_GAMECUBE 1
/* libogc GameCube: bus/4000 ticks per ms, bus/4 ticks per second. */
#define TB_TIMER_CLOCK (162000000 / 4000)
#define PPC_TIMER_CLOCK (162000000 / 4)
typedef uint64_t u64;
static u64 fake_ticks;
static u64 gettime(void) { return fake_ticks; }
static u64 diff_ticks(u64 start, u64 end) { return end - start; }
'''
        fixture += function.group(0)
        fixture += r'''
int main(void) {
    const u64 start = (UINT64_C(1) << 32) - PPC_TIMER_CLOCK / 4;
    fake_ticks = start;
    assert(Platform_DoubleTime() == 0.0);
    fake_ticks += PPC_TIMER_CLOCK / 2;
    assert(Platform_DoubleTime() == 0.5);
    assert(Platform_DoubleTime() == 0.5);
    fake_ticks = start + (u64)PPC_TIMER_CLOCK * 123 + PPC_TIMER_CLOCK * 3 / 4;
    assert(Platform_DoubleTime() == 123.75);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="gamecube-clock-") as tmp:
            path = Path(tmp)
            (path / "fixture.c").write_text(fixture)
            result = subprocess.run(
                [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", str(path / "fixture.c"),
                 "-o", str(path / "fixture")], capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(path / "fixture")], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
