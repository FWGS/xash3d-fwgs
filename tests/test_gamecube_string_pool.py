"""Exercise the production pool lookup with both allocation header formats."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GameCubeStringPoolTests(unittest.TestCase):
    def test_renderer_arena_aligned_base_and_release_identity(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "ref/gx/r_gcmap.c").read_text()
        base = source.split("static byte *R_GcmapStaticArenaBase( void )", 1)[1].split(
            "\nstatic void R_GcmapReportStaticArenaTelemetry", 1)[0]
        arena = source.split("void *R_GCBorrowMapLoadStaticArena(", 1)[1].split(
            "\nvoid R_GcmapTrimForMapLoad", 1)[0]
        fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef unsigned char byte;
typedef int qboolean;
#define true 1
#define false 0
static struct {
    byte prefix[3];
    byte colormap[64], screen[32], screen32[32], addmap[32], modmap[32],
         alphamap[64], mapload_pad[128];
} vid __attribute__((aligned(32)));
static qboolean gc_renderer_trimmed, gc_static_map_arena_in_use, gc_static_map_arena_dirty;
static void log_msg(const char *fmt, ...) { (void)fmt; }
static struct { void (*Con_Reportf)(const char *, ...); } gEngfuncs = { log_msg };
static const char *Q_memprint(size_t n) { (void)n; return "size"; }
static void R_GcmapReportStaticArenaTelemetry(void) {}
'''
        fixture += "static byte *R_GcmapStaticArenaBase( void )" + base
        fixture += "void *R_GCBorrowMapLoadStaticArena(" + arena
        fixture += r'''
int main(void) {
    size_t capacity = 0;
    byte *raw = &vid.colormap[0];
    assert(R_GCBorrowMapLoadStaticArena(1, &capacity) == NULL);
    gc_renderer_trimmed = true;
    byte *p = R_GCBorrowMapLoadStaticArena(1, &capacity);
    assert(p && ((uintptr_t)p & 31) == 0 && p >= raw);
    assert(p + capacity == &vid.mapload_pad[0] + sizeof(vid.mapload_pad));
    assert(R_GCIsMapLoadStaticArena(p) && !R_GCIsMapLoadStaticArena(raw));
    assert(!R_GCReleaseMapLoadStaticArena(raw));
    assert(R_GCReleaseMapLoadStaticArena(p) && gc_static_map_arena_dirty);
    assert(!R_GCReleaseMapLoadStaticArena(p));
    assert(!R_GCBorrowMapLoadStaticArena(capacity + 1, NULL));
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

    def test_map_arena_tail_alignment_with_unaligned_base(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "engine/platform/gamecube/mem_gamecube.c").read_text()
        function = source.split("void *GC_MapLoadBufferTailAlloc(", 1)[1].split(
            "\nvoid GC_DiscardMapLoadBuffer", 1)[0]
        fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef unsigned char byte;
static byte storage[600] __attribute__((aligned(32)));
static byte *gc_mapload_buf;
static size_t gc_mapload_buf_size, gc_mapload_tail_highwater;
static int gc_mapload_buf_in_use, entity_reserved;
static int SV_GCPrivateOverflowReserved(void) { return entity_reserved; }
static void Con_Reportf(const char *format, ...) { (void)format; }
'''
        fixture += "void *GC_MapLoadBufferTailAlloc(" + function
        fixture += r'''
int main(void) {
    for(size_t offset=0; offset<32; offset++) {
        gc_mapload_buf = storage + offset;
        gc_mapload_buf_size = 512;
        gc_mapload_tail_highwater = 0;
        gc_mapload_buf_in_use = 1;
        byte *first = GC_MapLoadBufferTailAlloc(3, 64);
        assert(first && ((uintptr_t)first & 31) == 0);
        assert(first >= gc_mapload_buf + 3);
        assert(GC_MapLoadBufferTailAlloc(0, 16) == NULL);
        byte *second = GC_MapLoadBufferTailAlloc(gc_mapload_tail_highwater, 32);
        assert(second && ((uintptr_t)second & 31) == 0 && second >= first + 64);
        assert(GC_MapLoadBufferTailAlloc(SIZE_MAX, 16) == NULL);
        assert(GC_MapLoadBufferTailAlloc(gc_mapload_tail_highwater, 512) == NULL);
        gc_mapload_buf_in_use = 0;
        assert(GC_MapLoadBufferTailAlloc(0, 16) == NULL);
        entity_reserved = 1;
        byte *retained = GC_MapLoadBufferTailAlloc(gc_mapload_tail_highwater, 16);
        assert(retained && ((uintptr_t)retained & 31) == 0 && retained >= second + 32);
        entity_reserved = 0;
    }
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

    def test_texture_budget_selects_least_aggressive_split_that_fits(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "engine/common/mod_bmodel.c").read_text()
        function = source.split("static unsigned Mod_GCWorldPackSplitDim(", 1)[1].split(
            "\nstatic const gc_wt_stored_t *Mod_GCWorldStoreFind", 1)[0]
        fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef unsigned char byte;
typedef int qboolean;
typedef struct { char name[16]; uint16_t width, height; byte levels, flags;
                 uint16_t pad; uint32_t offset; } gc_wt_entry_t;
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define Q_max(a,b) ((a) > (b) ? (a) : (b))
#define GC_WT_SPLIT_DIM 64
#define GC_WT_MIN_HOT_BYTES (32 * 1024)
'''
        fixture += "static unsigned Mod_GCWorldPackSplitDim(" + function
        fixture += r'''
int main(void) {
    gc_wt_entry_t t[4] = {0};
    t[0].width=t[0].height=128; t[0].levels=5; t[0].offset=0;
    t[1].width=t[1].height=64;  t[1].levels=4; t[1].offset=10912;
    t[2].width=t[2].height=32;  t[2].levels=3; t[2].offset=13632;
    t[3].width=t[3].height=16;  t[3].levels=2; t[3].offset=14304;
    assert(Mod_GCWorldPackSplitDim(t, 4, 14464, 100000, 128) == 64);
    assert(Mod_GCWorldPackSplitDim(t, 4, 14464, 128+32768+5000, 128) == 32);
    assert(Mod_GCWorldPackSplitDim(t, 4, 14464, 128+32768+4000, 128) == 16);
    assert(Mod_GCWorldPackSplitDim(t, 4, 14464, 128+32768+3600, 128) == 8);
    assert(Mod_GCWorldPackSplitDim(t, 4, 14464, 128+32768, 128) == 8);
    t[2].offset = 100;
    assert(Mod_GCWorldPackSplitDim(t, 4, 14464, 100000, 128) == 64);
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

    def test_map_arena_discard_waits_for_entity_overflow(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "engine/platform/gamecube/mem_gamecube.c").read_text()
        function = source.split("void GC_DiscardMapLoadBuffer( void )", 1)[1].split(
            "\nvoid GC_BeginMapLoadMemoryOpt", 1)[0]
        fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
typedef unsigned char byte;
static byte arena[64];
static byte *gc_mapload_buf = arena;
static size_t gc_mapload_buf_size = sizeof(arena);
static int gc_mapload_buf_in_use, entity_reserved, release_calls;
static int SV_GCPrivateOverflowReserved(void) { return entity_reserved; }
static int R_GCReleaseMapLoadStaticArena(void *p) {
    assert(p == arena); release_calls++; return 1;
}
#define Con_Reportf(...) ((void)0)
'''
        fixture += "void GC_DiscardMapLoadBuffer( void )" + function
        fixture += r'''
int main(void) {
    entity_reserved = 1;
    GC_DiscardMapLoadBuffer();
    assert(gc_mapload_buf == arena && release_calls == 0);
    entity_reserved = 0; gc_mapload_buf_in_use = 1;
    GC_DiscardMapLoadBuffer();
    assert(gc_mapload_buf == arena && release_calls == 0);
    gc_mapload_buf_in_use = 0;
    GC_DiscardMapLoadBuffer();
    assert(gc_mapload_buf == NULL && gc_mapload_buf_size == 0 && release_calls == 1);
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

    def test_private_overflow_reservation_and_reuse(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "engine/server/sv_game.c").read_text()
        functions = source.split("void SV_GCReservePrivateOverflow(", 1)[1].split(
            "\nstatic void *SV_GCPrivateSlabAlloc(", 1)[0]
        fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef unsigned char byte;
typedef int qboolean;
#define GC_ENTITY_OVERFLOW_BYTES (64u * 1024u)
static byte storage[GC_ENTITY_OVERFLOW_BYTES] __attribute__((aligned(32)));
static byte *gc_ent_priv_overflow;
static byte gc_ent_priv_overflow_bits[GC_ENTITY_OVERFLOW_BYTES / 16 / 8];
static size_t gc_ent_priv_overflow_capacity, gc_ent_priv_overflow_used;
static int calls;
static void *GC_MapLoadBufferTailAlloc(size_t after, size_t bytes) {
	    calls++; return after == 128 && bytes <= sizeof(storage) ? storage : NULL;
}
#define Con_Reportf(...) ((void)0)
'''
        fixture += "void SV_GCReservePrivateOverflow(" + functions
        fixture += r'''
int main(void) {
    SV_GCReservePrivateOverflow(0, 49152);
    assert(!SV_GCPrivateOverflowReserved());
    assert(SV_GCPrivateOverflowAlloc(16) == NULL);
    SV_GCReservePrivateOverflow(128, 49152);
    assert(SV_GCPrivateOverflowReserved() && calls == 2);
    assert(gc_ent_priv_overflow_capacity == 49152);
    SV_GCReservePrivateOverflow(128, 49152);
    assert(calls == 2);
    byte *a = SV_GCPrivateOverflowAlloc(32);
    byte *b = SV_GCPrivateOverflowAlloc(48);
    byte *c = SV_GCPrivateOverflowAlloc(48);
    assert(a == storage && b == a + 32 && c == b + 48);
    assert(SV_GCPrivateDataIsOverflow(a) && SV_GCPrivateDataIsOverflow(c + 47));
    assert(!SV_GCPrivateDataIsOverflow(storage + 49152));
    memset(c, 42, 48);
    SV_GCPrivateOverflowFree(b, 48);
    byte *d = SV_GCPrivateOverflowAlloc(17);
    assert(d == b && c[0] == 42 && c[47] == 42);
    SV_GCPrivateOverflowFree(a, 32); SV_GCPrivateOverflowFree(c, 48);
    SV_GCPrivateOverflowFree(d, 17);
    assert(gc_ent_priv_overflow_used == 0);
    assert(SV_GCPrivateOverflowAlloc(49152) == storage);
    assert(SV_GCPrivateOverflowAlloc(16) == NULL);
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

    def test_entity_overflow_budget_preserves_world_tree_space(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "engine/common/mod_bmodel.c").read_text()
        function = source.split("static size_t Mod_GCEntityOverflowBudget(", 1)[1].split(
            "/* Keep small textures fully resident", 1)[0]
        fixture = r'''
#include <assert.h>
#include <stddef.h>
#define GC_ENTITY_OVERFLOW_BYTES (64u * 1024u)
'''
        fixture += "static size_t Mod_GCEntityOverflowBudget(" + function
        fixture += r'''
int main(void) {
    size_t b = Mod_GCEntityOverflowBudget(555188, 193984, 301236);
    assert(b >= 32u * 1024u && b <= 64u * 1024u && (b & 31u) == 0);
    assert(555188 - 193984 - 301236 - b >= 8u * 1024u);
    assert(Mod_GCEntityOverflowBudget(1000, 2000, 3000) == GC_ENTITY_OVERFLOW_BYTES);
    assert(Mod_GCEntityOverflowBudget(2u * 1024u * 1024u, 193984, 301236) == GC_ENTITY_OVERFLOW_BYTES);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "fixture.c").write_text(fixture)
            subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(path / "fixture.c"), "-o", str(path / "fixture")], check=True)
            subprocess.run([str(path / "fixture")], check=True)

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
