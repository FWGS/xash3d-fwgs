"""Exercise the production pool lookup with both allocation header formats."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GameCubeStringPoolTests(unittest.TestCase):
    def test_private_slab_reuses_holes_without_moving_live_objects(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "engine/server/sv_game.c").read_text()
        functions = source.split("static void *SV_GCPrivateSlabAlloc(", 1)[1].split(
            "/* Entity key/value", 1)[0]
        fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef unsigned char byte;
static byte gc_ent_priv_slab[128] __attribute__((aligned(16)));
static byte gc_ent_priv_slab_bits[1];
static size_t gc_ent_priv_slab_used;
'''
        fixture += "static void *SV_GCPrivateSlabAlloc(" + functions
        fixture += r'''
int main(void) {
    byte *a = SV_GCPrivateSlabAlloc(32);
    byte *b = SV_GCPrivateSlabAlloc(48);
    byte *c = SV_GCPrivateSlabAlloc(48);
    assert(a && b && c && gc_ent_priv_slab_used == 128);
    memset(a, 11, 32); memset(c, 22, 48);
    assert(SV_GCPrivateSlabAlloc(16) == NULL);
    SV_GCPrivateSlabFree(b, 48);
    byte *d = SV_GCPrivateSlabAlloc(17);
    byte *e = SV_GCPrivateSlabAlloc(16);
    assert(d == b && e == b + 32);
    assert(((uintptr_t)d & 15) == 0);
    for(int i = 0; i < 32; i++) assert(a[i] == 11);
    for(int i = 0; i < 48; i++) assert(c[i] == 22);
    SV_GCPrivateSlabFree(a, 32); SV_GCPrivateSlabFree(c, 48);
    SV_GCPrivateSlabFree(d, 17); SV_GCPrivateSlabFree(e, 16);
    assert(gc_ent_priv_slab_used == 0);
    assert(SV_GCPrivateSlabAlloc(128) == a);
    assert(SV_GCPrivateSlabAlloc(0) == NULL);
    assert(SV_GCPrivateSlabAlloc(129) == NULL);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "fixture.c").write_text(fixture)
            subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", str(path / "fixture.c"),
                            "-o", str(path / "fixture")], check=True)
            subprocess.run([str(path / "fixture")], check=True)

    def test_keyvalue_cleanup_does_not_free_borrowed_slab_strings(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "engine/server/sv_game.c").read_text()
        ownership = source.split("static qboolean SV_GCKeyValueIsSlab(", 1)[1].split("\n#else", 1)[0]
        helper = source.split("static void SV_FreeKeyValueString(", 1)[1].split(
            "\nstatic void SV_FreeKeyValueStrings(", 1)[0]
        fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define XASH_GAMECUBE 1
typedef int qboolean;
static char gc_kv_slab[64], owned[8];
#define slab gc_kv_slab
static int frees;
static void Mem_Free(void *p) { assert(p == NULL || p == owned); if(p) frees++; }
'''
        fixture += "static qboolean SV_GCKeyValueIsSlab(" + ownership
        fixture += "static void SV_FreeKeyValueString(" + helper
        fixture += r'''
int main(void) {
    SV_FreeKeyValueString(slab);
    SV_FreeKeyValueString(slab + sizeof(slab) - 1);
    SV_FreeKeyValueString(NULL);
    assert(frees == 0);
    SV_FreeKeyValueString(owned);
    assert(frees == 1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "fixture.c").write_text(fixture)
            subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(path / "fixture.c"), "-o", str(path / "fixture")], check=True)
            subprocess.run([str(path / "fixture")], check=True)

    def test_lookup_preserves_pool_ownership_and_exact_string_boundaries(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "engine/common/zone.c").read_text()
        function = source.split("const char *Mem_FindPoolString(", 1)[1].split("\n#endif", 1)[0]
        fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
typedef unsigned poolhandle_t;
typedef struct big { struct big *next; size_t size; } memheader_t;
typedef struct small { struct small *next; unsigned short size; } memheader_small_t;
typedef struct { memheader_t *chain; memheader_small_t *chain_small; } mempool_t;
static mempool_t pools[2];
static mempool_t *Mem_FindPool(poolhandle_t p) { return p < 2 ? &pools[p] : NULL; }
#define Q_strlen strlen
'''
        fixture += "const char *Mem_FindPoolString(" + function
        fixture += r'''
int main(void) {
    struct { memheader_t h; char text[11]; } big = {{NULL, 11}, "env_sprite"};
    struct { memheader_small_t h; char text[6]; } small = {{NULL, 6}, "light"};
    struct { memheader_t h; char text[11]; } other = {{NULL, 11}, "env_sprite"};
    pools[0].chain = &big.h; pools[0].chain_small = &small.h;
    pools[1].chain = &other.h;
    assert(Mem_FindPoolString(0, "env_sprite") == big.text);
    assert(Mem_FindPoolString(0, "light") == small.text);
    assert(Mem_FindPoolString(1, "env_sprite") == other.text);
    assert(Mem_FindPoolString(0, "env_sprit") == NULL);
    assert(Mem_FindPoolString(0, "env_sprite_extra") == NULL);
    assert(Mem_FindPoolString(0, "ENV_SPRITE") == NULL);
    assert(Mem_FindPoolString(0, "") == NULL);
    assert(Mem_FindPoolString(0, NULL) == NULL);
    assert(Mem_FindPoolString(99, "light") == NULL);
    pools[0].chain = NULL; pools[0].chain_small = NULL;
    assert(Mem_FindPoolString(0, "env_sprite") == NULL);
    assert(Mem_FindPoolString(1, "env_sprite") == other.text);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "fixture.c").write_text(fixture)
            subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(path / "fixture.c"), "-o", str(path / "fixture")], check=True)
            subprocess.run([str(path / "fixture")], check=True)
