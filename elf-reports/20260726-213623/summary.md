# GameCube ELF Memory Report

- Generated: 2026-07-26T21:36:23.614249+00:00
- ELF file: `/home/tim/Desktop/xash3d-gamecube-agent/OUT/bin/xash`
- File size: 20,847,632 bytes (19.88 MiB)
- Architecture: unknown

## Section Sizes

| Section | Size (bytes) | Size (hex) | Address |
|---|---:|---|---|
| .init | 160 | 0xa0 | 0x80003100 |
| .text | 3,129,568 | 0x2fc0e0 | 0x800031a0 |
| .rodata | 447,188 | 0x6d2d4 | 0x802ff280 |
| .sdata2 | 82 | 0x52 | 0x8036c554 |
| .eh_frame_hdr | 39,100 | 0x98bc | 0x8036c5a8 |
| .eh_frame | 346,544 | 0x549b0 | 0x80375e64 |
| .gcc_except_table | 60 | 0x3c | 0x803ca814 |
| .ctors | 24 | 0x18 | 0x803ca850 |
| .dtors | 16 | 0x10 | 0x803ca868 |
| .data | 45,016 | 0xafd8 | 0x803ca878 |
| .sdata | 368 | 0x170 | 0x803d5850 |
| .sbss | 1,192 | 0x4a8 | 0x803d59c0 |
| .bss | 11,751,800 | 0xb35178 | 0x803d5e68 |
| .comment | 74 | 0x4a | 0x0 |
| .gnu.attributes | 18 | 0x12 | 0x0 |
| .debug_aranges | 61,208 | 0xef18 | 0x0 |
| .debug_info | 7,304,749 | 0x6f762d | 0x0 |
| .debug_abbrev | 519,070 | 0x7eb9e | 0x0 |
| .debug_line | 3,885,057 | 0x3b4801 | 0x0 |
| .debug_frame | 288 | 0x120 | 0x0 |
| .debug_str | 408,322 | 0x63b02 | 0x0 |
| .debug_line_str | 542 | 0x21e | 0x0 |
| .debug_loclists | 3,860,129 | 0x3ae6a1 | 0x0 |
| .debug_rnglists | 516,409 | 0x7e139 | 0x0 |

## Memory Summary

- **.text** (code): 3,129,568 bytes (3056.22 KiB)
- **.data** (initialized): 45,016 bytes (43.96 KiB)
- **.rodata** (read-only): 447,188 bytes (436.71 KiB)
- **.bss** (uninitialized): 11,751,800 bytes (11.21 MiB)
- **Total section size**: 32,316,984 bytes (30.82 MiB)

## Symbol Statistics

- Total symbols: 8146
- Total symbol size: 15,135,807 bytes
- Max symbol size: 3,670,048 bytes
- Min symbol size: 1 bytes

### Symbol Type Distribution

| Type | Count |
|---|---|
| B | 212 |
| D | 335 |
| R | 1 |
| T | 3133 |
| V | 36 |
| W | 69 |
| b | 1315 |
| d | 1279 |
| r | 11 |
| t | 1755 |

## Top 50 Largest Symbols

| Name | Size (bytes) | Type |
|---|---:|---|
| vid | 3,670,048 | B |
| gc_gcmap_bootstrap_entities | 768,000 | b |
| cl | 652,944 | B |
| sv_ | 637,968 | B |
| s_knownSfx | 524,288 | b |
| gpMove.14 | 325,072 | b |
| gpMove.32 | 325,072 | b |
| r_images | 319,488 | b |
| net | 295,756 | b |
| gx_fifo | 262,144 | b |
| r_leafkeys | 262,144 | B |
| clgame | 259,880 | B |
| mod_known | 200,704 | b |
| g_studio | 180,320 | b |
| gc_avi_static_frame | 153,600 | b |
| gc_cinematic_pixels | 153,600 | b |
| gc_gcmap_static_viewbuffer | 153,600 | b |
| gc_gcmap_static_zbuffer | 153,600 | b |
| gc_probe_rgb565 | 153,600 | b |
| gc_tiled_rgb565 | 153,600 | b |
| gc_lowres_surfcache_store | 131,104 | b |
| gc_rgb565_to_sw | 131,072 | b |
| s_mainStack | 131,072 | b |
| cls | 101,128 | B |
| gc_studio_bss | 86,016 | b |
| gc_newgame_cap_faces | 62,720 | b |
| con | 56,972 | b |
| gc_zip_inflate_scratch | 49,152 | b |
| __compound_literal.0 | 48,640 | b |
| frame_ents.1 | 44,548 | b |
| gc_gcmap_bootstrap_packet_entities | 43,520 | b |
| blocklights | 40,960 | b |
| fatal_panel.36 | 38,400 | b |
| gc_loading_bg | 38,400 | b |
| svs_ | 33,480 | B |
| msg_buf.2 | 33,280 | b |
| ispow | 32,828 | b |
| net_message_buffer | 32,816 | B |
| cmd_text | 32,772 | b |
| filteredcmd_text | 32,772 | b |
| gc_parent_stack.14 | 32,768 | b |
| r_gx_tex_world_pool | 32,768 | b |
| celt_encode_with_ec | 26,436 | T |
| gc_newgame_draw_surfs | 25,600 | b |
| voice | 25,200 | B |
| xrcon | 24,720 | b |
| gc_probe_edges_store | 24,672 | b |
| gc_probe_surfaces_store | 24,672 | b |
| gc_singleplayer_frames | 24,512 | b |
| tonemasks | 22,848 | d |
