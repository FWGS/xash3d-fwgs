#!/usr/bin/env python3
"""
Convert ELF to DOL format for Nintendo GameCube.
This script provides a reliable alternative to elf2dol from devkitPro.
"""

import struct
import sys


def parse_elf_segments(elf_data):
    """Parse PT_LOAD segments from ELF file."""
    if elf_data[:4] != b'\x7fELF':
        raise ValueError("Not a valid ELF file")

    # Determine ELF class (32-bit or 64-bit)
    elf_class = elf_data[4]
    
    if elf_class == 1:  # 32-bit ELF
        is_64bit = False
        e_entry = struct.unpack('>I', elf_data[0x18:0x1C])[0]
        e_phoff = struct.unpack('>I', elf_data[0x1C:0x20])[0]
        e_phnum = struct.unpack('<H', elf_data[0x36:0x38])[0]
        e_phentsize = struct.unpack('<H', elf_data[0x3A:0x3C])[0]
        phdr_size = 32
    elif elf_class == 2:  # 64-bit ELF
        is_64bit = True
        e_entry = struct.unpack('>Q', elf_data[0x18:0x20])[0]
        e_phoff = struct.unpack('>Q', elf_data[0x20:0x28])[0]
        e_phnum = struct.unpack('<H', elf_data[0x3C:0x3E])[0]
        e_phentsize = struct.unpack('<H', elf_data[0x3A:0x3C])[0]
        phdr_size = 56
    else:
        raise ValueError(f"Unknown ELF class: {elf_class}")

    segments = []
    for i in range(e_phnum):
        offset = e_phoff + i * phdr_size
        
        if is_64bit:
            p_type = struct.unpack('>I', elf_data[offset:offset+4])[0]
            p_flags = struct.unpack('>I', elf_data[offset+4:offset+8])[0]
            p_offset = struct.unpack('>Q', elf_data[offset+8:offset+16])[0]
            p_vaddr = struct.unpack('>Q', elf_data[offset+16:offset+24])[0]
            p_paddr = struct.unpack('>Q', elf_data[offset+24:offset+32])[0]
            p_filesz = struct.unpack('>Q', elf_data[offset+32:offset+40])[0]
            p_memsz = struct.unpack('>Q', elf_data[offset+40:offset+48])[0]
        else:
            p_type = struct.unpack('>I', elf_data[offset:offset+4])[0]
            p_offset = struct.unpack('>I', elf_data[offset+4:offset+8])[0]
            p_vaddr = struct.unpack('>I', elf_data[offset+8:offset+12])[0]
            p_paddr = struct.unpack('>I', elf_data[offset+12:offset+16])[0]
            p_filesz = struct.unpack('>I', elf_data[offset+16:offset+20])[0]
            p_memsz = struct.unpack('>I', elf_data[offset+20:offset+24])[0]

        if p_type == 1:  # PT_LOAD
            segments.append({
                'offset': p_offset,
                'vaddr': p_vaddr,
                'paddr': p_paddr,
                'filesz': p_filesz,
                'memsz': p_memsz
            })

    # Sort by virtual address
    segments.sort(key=lambda x: x['vaddr'])
    
    # Mark first segment as text, rest as data
    # This is a heuristic for PowerPC ELF files
    if len(segments) > 0:
        segments[0]['is_text'] = True
        for i in range(1, len(segments)):
            segments[i]['is_text'] = False
    
    return segments, e_entry


def create_dol_header(segments, entry_point):
    """Create DOL header (780 bytes)."""
    header = bytearray(780)  # 0x30C bytes

    # DOL header structure (780 bytes = 0x30C):
    # 0x00-0x17: Text segment file offsets (6 * 4 = 24 bytes)
    # 0x18-0x2F: Data segment file offsets (6 * 4 = 24 bytes)
    # 0x30-0x47: Text segment memory addresses (6 * 4 = 24 bytes)
    # 0x48-0x5F: Data segment memory addresses (6 * 4 = 24 bytes)
    # 0x60-0x77: Text segment sizes (6 * 4 = 24 bytes)
    # 0x78-0x8F: Data segment sizes (6 * 4 = 24 bytes)
    # 0x90-0x93: BSS address
    # 0x94-0x97: BSS size
    # 0x98-0x9B: Entry point

    # Calculate file offsets for segments (after 780-byte header)
    file_offset = 780  # 0x30C

    text_idx = 0
    data_idx = 0

    for seg in segments:
        if seg['filesz'] == 0:
            continue

        if seg.get('is_text', seg['vaddr'] >= 0x80000000):  # Text segment
            if text_idx < 6:
                # Text segment file offset at 0x00 + text_idx * 4
                header[0x00 + text_idx * 4:0x04 + text_idx * 4] = struct.pack('>I', file_offset)
                # Text segment memory address at 0x30 + text_idx * 4
                header[0x30 + text_idx * 4:0x34 + text_idx * 4] = struct.pack('>I', seg['vaddr'])
                # Text segment size at 0x60 + text_idx * 4
                header[0x60 + text_idx * 4:0x64 + text_idx * 4] = struct.pack('>I', seg['filesz'])
                text_idx += 1
        else:  # Data segment
            if data_idx < 6:
                # Data segment file offset at 0x18 + data_idx * 4
                header[0x18 + data_idx * 4:0x1C + data_idx * 4] = struct.pack('>I', file_offset)
                # Data segment memory address at 0x48 + data_idx * 4
                header[0x48 + data_idx * 4:0x4C + data_idx * 4] = struct.pack('>I', seg['vaddr'])
                # Data segment size at 0x78 + data_idx * 4
                header[0x78 + data_idx * 4:0x7C + data_idx * 4] = struct.pack('>I', seg['filesz'])
                data_idx += 1

        file_offset += seg['filesz']

    # BSS info (from the last segment if it has memsz > filesz)
    if segments:
        last_seg = segments[-1]
        if last_seg['memsz'] > last_seg['filesz']:
            header[0x90:0x94] = struct.pack('>I', last_seg['vaddr'] + last_seg['filesz'])
            header[0x94:0x98] = struct.pack('>I', last_seg['memsz'] - last_seg['filesz'])

    # Entry point
    header[0x98:0x9C] = struct.pack('>I', entry_point)

    return header


def elf_to_dol(elf_path, dol_path):
    """Convert ELF file to DOL format."""
    with open(elf_path, 'rb') as f:
        elf_data = f.read()

    segments, entry_point = parse_elf_segments(elf_data)

    print(f"ELF entry point: 0x{entry_point:08X}")
    print(f"Segments: {len(segments)}")
    for i, seg in enumerate(segments):
        print(f"  [{i}] vaddr=0x{seg['vaddr']:08X}, filesz=0x{seg['filesz']:X}, memsz=0x{seg['memsz']:X}")

    # Create DOL header
    header = create_dol_header(segments, entry_point)

    # Build DOL file
    dol_data = bytearray(header)

    # Add segment data
    for seg in segments:
        if seg['filesz'] > 0:
            dol_data.extend(elf_data[seg['offset']:seg['offset']+seg['filesz']])

    # Write DOL file
    with open(dol_path, 'wb') as f:
        f.write(dol_data)

    print(f"\nDOL file created: {len(dol_data)} bytes")
    print(f"  Header: 780 bytes")
    for i, seg in enumerate(segments):
        if seg['filesz'] > 0:
            print(f"  Segment {i}: {seg['filesz']} bytes at offset 0x{dol_data[:780][i*4 if i < 6 else (i-6)*4+24]:X}")


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.elf> <output.dol>")
        sys.exit(1)

    elf_to_dol(sys.argv[1], sys.argv[2])
