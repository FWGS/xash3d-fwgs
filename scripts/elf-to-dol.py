#!/usr/bin/env python3
"""
Convert a PowerPC ELF executable into a Nintendo GameCube/Wii DOL image.
"""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from pathlib import Path


DOL_HEADER_SIZE = 0x100
MAX_TEXT_SECTIONS = 7
MAX_DATA_SECTIONS = 11
PT_LOAD = 1
PF_X = 0x1


@dataclass
class Segment:
    file_offset: int
    virt_addr: int
    file_size: int
    mem_size: int
    is_text: bool


def align(value: int, boundary: int) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def unpack_half(data: bytes, offset: int, is_big_endian: bool) -> int:
    fmt = ">H" if is_big_endian else "<H"
    return struct.unpack_from(fmt, data, offset)[0]


def unpack_word(data: bytes, offset: int, is_big_endian: bool) -> int:
    fmt = ">I" if is_big_endian else "<I"
    return struct.unpack_from(fmt, data, offset)[0]


def parse_elf_segments(elf_data: bytes) -> tuple[list[Segment], int]:
    if elf_data[:4] != b"\x7fELF":
        raise ValueError("Not a valid ELF file")
    if elf_data[4] != 1:
        raise ValueError("Only ELF32 inputs are supported")

    data_encoding = elf_data[5]
    if data_encoding == 1:
        is_big_endian = False
    elif data_encoding == 2:
        is_big_endian = True
    else:
        raise ValueError(f"Unknown ELF data encoding: {data_encoding}")

    entry_point = unpack_word(elf_data, 0x18, is_big_endian)
    phoff = unpack_word(elf_data, 0x1C, is_big_endian)
    phentsize = unpack_half(elf_data, 0x2A, is_big_endian)
    phnum = unpack_half(elf_data, 0x2C, is_big_endian)

    if phentsize < 32:
        raise ValueError(f"Unexpected program header size: {phentsize}")

    segments: list[Segment] = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        p_type = unpack_word(elf_data, offset + 0x00, is_big_endian)
        if p_type != PT_LOAD:
            continue

        p_offset = unpack_word(elf_data, offset + 0x04, is_big_endian)
        p_vaddr = unpack_word(elf_data, offset + 0x08, is_big_endian)
        p_filesz = unpack_word(elf_data, offset + 0x10, is_big_endian)
        p_memsz = unpack_word(elf_data, offset + 0x14, is_big_endian)
        p_flags = unpack_word(elf_data, offset + 0x18, is_big_endian)

        segments.append(
            Segment(
                file_offset=p_offset,
                virt_addr=p_vaddr,
                file_size=p_filesz,
                mem_size=p_memsz,
                is_text=bool(p_flags & PF_X),
            )
        )

    if not segments:
        raise ValueError("No PT_LOAD segments found")

    segments.sort(key=lambda segment: (not segment.is_text, segment.virt_addr))
    return segments, entry_point


def create_dol_header(segments: list[Segment], entry_point: int) -> bytearray:
    header = bytearray(DOL_HEADER_SIZE)
    current_output_offset = DOL_HEADER_SIZE
    text_index = 0
    data_index = 0
    bss_start: int | None = None
    bss_end: int | None = None

    for segment in segments:
        if segment.file_size == 0 and segment.mem_size == 0:
            continue

        current_output_offset = align(current_output_offset, 0x20)

        if segment.is_text:
            if text_index >= MAX_TEXT_SECTIONS:
                raise ValueError("Too many text segments for DOL")
            index = text_index
            struct.pack_into(">I", header, 0x00 + index * 4, current_output_offset)
            struct.pack_into(">I", header, 0x48 + index * 4, segment.virt_addr)
            struct.pack_into(">I", header, 0x90 + index * 4, segment.file_size)
            text_index += 1
        else:
            if data_index >= MAX_DATA_SECTIONS:
                raise ValueError("Too many data segments for DOL")
            index = data_index
            struct.pack_into(">I", header, 0x1C + index * 4, current_output_offset)
            struct.pack_into(">I", header, 0x64 + index * 4, segment.virt_addr)
            struct.pack_into(">I", header, 0xAC + index * 4, segment.file_size)
            data_index += 1

        current_output_offset += segment.file_size

        if segment.mem_size > segment.file_size:
            segment_bss_start = segment.virt_addr + segment.file_size
            segment_bss_end = segment.virt_addr + segment.mem_size
            bss_start = segment_bss_start if bss_start is None else min(bss_start, segment_bss_start)
            bss_end = segment_bss_end if bss_end is None else max(bss_end, segment_bss_end)

    if bss_start is not None and bss_end is not None and bss_end > bss_start:
        struct.pack_into(">I", header, 0xD8, bss_start)
        struct.pack_into(">I", header, 0xDC, bss_end - bss_start)

    struct.pack_into(">I", header, 0xE0, entry_point)
    return header


def elf_to_dol(elf_path: Path, dol_path: Path) -> None:
    elf_data = elf_path.read_bytes()
    segments, entry_point = parse_elf_segments(elf_data)
    header = create_dol_header(segments, entry_point)

    dol_data = bytearray(header)
    current_output_offset = DOL_HEADER_SIZE
    for segment in segments:
        if segment.file_size == 0:
            continue
        current_output_offset = align(current_output_offset, 0x20)
        if len(dol_data) < current_output_offset:
            dol_data.extend(b"\0" * (current_output_offset - len(dol_data)))
        dol_data.extend(elf_data[segment.file_offset:segment.file_offset + segment.file_size])
        current_output_offset += segment.file_size

    dol_path.write_bytes(dol_data)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"Usage: {argv[0]} <input.elf> <output.dol>", file=sys.stderr)
        return 1

    elf_to_dol(Path(argv[1]), Path(argv[2]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
