"""Replay the production GameCube TAS player with a native C runtime."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GameCubeTasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler unavailable")
        cls.temp = tempfile.TemporaryDirectory(prefix="gamecube-tas-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.path = Path(cls.temp.name)
        (cls.path / "ogc").mkdir()
        (cls.path / "ogc/pad.h").write_text("""
#pragma once
#define PAD_BUTTON_LEFT  0x0001
#define PAD_BUTTON_RIGHT 0x0002
#define PAD_BUTTON_DOWN  0x0004
#define PAD_BUTTON_UP    0x0008
#define PAD_TRIGGER_Z    0x0010
#define PAD_TRIGGER_R    0x0020
#define PAD_TRIGGER_L    0x0040
#define PAD_BUTTON_A     0x0100
#define PAD_BUTTON_B     0x0200
#define PAD_BUTTON_X     0x0400
#define PAD_BUTTON_Y     0x0800
#define PAD_BUTTON_START 0x1000
""")
        (cls.path / "common.h").write_text(r'''
#pragma once
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define XASH_GAMECUBE 1
#define XASH_INPUT 1
#define INPUT_GAMECUBE 1
#define true 1
#define false 0
#define MAX_QPATH 64
#define S_WARN ""
typedef int qboolean;
typedef unsigned int uint;
typedef unsigned char byte;
typedef long fs_offset_t;
#define Q_strnicmp strncasecmp
#define Q_snprintf snprintf
#define Q_strncpy(dst, src, size) snprintf(dst, size, "%s", src)
#define Con_Reportf printf
#define Mem_Free free
static const char *fixture_path;
static qboolean Sys_GetParmFromCmdLine(const char *key, char *name) {
    assert(!strcmp(key, "-gctas"));
    strcpy(name, "replay");
    return true;
}
static byte *FS_LoadFile(const char *path, fs_offset_t *size, qboolean gamedir) {
    assert(!strcmp(path, "tas/replay.tas") && !gamedir);
    FILE *file = fopen(fixture_path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    *size = ftell(file);
    rewind(file);
    byte *data = calloc((size_t)*size + 1, 1);
    assert(data && fread(data, 1, (size_t)*size, file) == (size_t)*size);
    fclose(file);
    return data;
}
''')
        source = cls.path / "fixture.c"
        source.write_text(
            '#include "common.h"\n'
            f'#include "{ROOT / "engine/platform/gamecube/tas_gamecube.c"}"\n'
            + r'''
static void poll(unsigned int frame) {
    unsigned short buttons = GC_TasPollButtons();
    signed char x, y, cx, cy;
    GC_TasGetSticks(&x, &y, &cx, &cy);
    printf("FRAME %u buttons=%u sticks=%d,%d,%d,%d active=%d complete=%d\n",
        frame, buttons, x, y, cx, cy, GC_TasActive(), GC_TasComplete());
}
int main(int argc, char **argv) {
    assert(argc >= 2);
    fixture_path = argv[1];
    assert(GC_TasTryLoad());
    if (argc > 2) {
        poll(0);
        poll(1);
        GC_TasResetPlayback();
        puts("RESET");
    }
    for (unsigned int frame = 0; frame < gc_tas.total_frames + 3; ++frame)
        poll(frame);
    return 0;
}
'''
        )
        cls.binary = cls.path / "fixture"
        subprocess.run(
            [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", f"-I{cls.path}", f"-I{ROOT / 'engine'}",
             str(source), "-o", str(cls.binary)],
            check=True, capture_output=True, text=True,
        )

    def replay(self, script, reset=False):
        script_path = self.path / "replay.tas"
        script_path.write_text(script)
        command = [str(self.binary), str(script_path)]
        if reset:
            command.append("reset")
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = result.stdout.split("RESET\n", 1)[-1]
        frames = [tuple(map(int, match)) for match in re.findall(
            r"^FRAME \d+ buttons=(\d+) sticks=(-?\d+),(-?\d+),(-?\d+),(-?\d+) "
            r"active=(\d+) complete=(\d+)$", output, re.MULTILINE
        )]
        return frames, output

    def test_buttons_and_sticks_stay_in_the_same_segment(self):
        frames, _ = self.replay("2 R 12 -34 56 -78\n1 A -11 22 -33 44\n2 B 9 8 7 6\n")
        self.assertEqual(frames[:5], [
            (0x20, 12, -34, 56, -78, 1, 0),
            (0x20, 12, -34, 56, -78, 1, 0),
            (0x100, -11, 22, -33, 44, 1, 0),
            (0x200, 9, 8, 7, 6, 1, 0),
            (0x200, 9, 8, 7, 6, 1, 0),
        ])

    def test_final_frame_is_consumed_before_completion(self):
        frames, output = self.replay("1 A 10 20 30 40\n")
        self.assertEqual(frames[0], (0x100, 10, 20, 30, 40, 1, 0))
        self.assertEqual(frames[1:], [(0, 0, 0, 0, 0, 0, 1)] * 3)
        self.assertEqual(output.count("probe tas complete name="), 1)
        self.assertEqual(output.count("probe gameplay input ready"), 1)
        self.assertLess(output.index("FRAME 0 "), output.index("probe tas complete"))

    def test_action_markers_match_current_controller_bindings(self):
        for button, action in (("R", "attack"), ("A", "jump"), ("B", "use"), ("Y", None)):
            with self.subTest(button=button):
                _, output = self.replay(f"1 {button} 0 0 0 0\n")
                actions = re.findall(r"probe gameplay action (\w+)", output)
                self.assertEqual(actions, [] if action is None else [action])

    def test_reset_replays_the_first_frame_and_action_markers(self):
        frames, output = self.replay("1 R 1 2 3 4\n1 A 5 6 7 8\n1 B 9 10 11 12\n", reset=True)
        self.assertEqual(frames[:3], [
            (0x20, 1, 2, 3, 4, 1, 0),
            (0x100, 5, 6, 7, 8, 1, 0),
            (0x200, 9, 10, 11, 12, 1, 0),
        ])
        self.assertEqual(re.findall(r"probe gameplay action (\w+)", output),
                         ["attack", "jump", "use"])

    def test_shipped_smoke_script_sends_attack_jump_use(self):
        script = (ROOT / "scripts/tas/gameplay-smoke.tas").read_text()
        frames, output = self.replay(script)
        self.assertEqual(len(frames), 35)
        self.assertEqual([frame[0] for frame in frames[:32]],
                         [0] * 4 + [0x20] * 16 + [0] * 8 + [0x100, 0, 0x200, 0])
        self.assertEqual([frame[2] for frame in frames[:32]], [64] * 4 + [0] * 28)
        self.assertEqual(re.findall(r"probe gameplay action (\w+)", output),
                         ["attack", "jump", "use"])


if __name__ == "__main__":
    unittest.main()
