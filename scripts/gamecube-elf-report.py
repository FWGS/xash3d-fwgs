#!/usr/bin/env python3
"""Generate static ELF, section, BSS, and symbol-size reports for GameCube port."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import NamedTuple


class SectionInfo(NamedTuple):
    name: str
    size: int
    addr: int
    offset: int
    align: int


class SymbolInfo(NamedTuple):
    name: str
    size: int
    type: str
    addr: int


@dataclass
class ELFReport:
    generated: str
    file_path: str
    file_size: int
    architecture: str
    sections: list[dict]
    bss_size: int
    data_size: int
    rodata_size: int
    text_size: int
    total_section_size: int
    symbol_stats: dict
    top_symbols: list[dict]


def run_cmd(cmd: list[str]) -> str:
    """Run a command and return its output."""
    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return result.stdout


def parse_sections(elf_path: Path) -> list[SectionInfo]:
    """Parse section headers from ELF file."""
    output = run_cmd([
        "/opt/devkitpro/devkitPPC/bin/powerpc-eabi-objdump",
        "-h",
        str(elf_path)
    ])
    
    sections = []
    
    for line in output.split("\n"):
        # Skip header lines
        if not line.strip() or line.startswith("Idx") or line.startswith("Sections:"):
            continue
        if "file format" in line:
            continue
        if not re.match(r'^\s+\d+\s+\.', line):
            continue
        
        # Parse the line - objdump format:
        # Idx Name          Size      VMA       LMA       File off  Algn
        #   0 .init         000000a0  80003100  80003100  000000a0  2**2
        parts = line.split()
        if len(parts) < 7:
            continue
        
        num = int(parts[0])
        name = parts[1]
        size = int(parts[2], 16)
        addr = int(parts[3], 16)
        offset = int(parts[4], 16)
        
        # The alignment is in format 2**N (e.g., 2**5)
        align_str = parts[5]
        align_match = re.match(r'2\*\*(\d+)', align_str)
        if align_match:
            align = int(align_match.group(1))
        else:
            align = 0
        
        sections.append(SectionInfo(
            name=name,
            size=size,
            addr=addr,
            offset=offset,
            align=align
        ))
    
    return sections


def parse_symbols(elf_path: Path) -> list[SymbolInfo]:
    """Parse symbol table from ELF file."""
    output = run_cmd([
        "/opt/devkitpro/devkitPPC/bin/powerpc-eabi-nm",
        "-S",
        str(elf_path)
    ])
    
    symbols = []
    symbol_re = re.compile(
        r"^(?P<addr>[0-9a-fA-F]+)\s+(?P<size>[0-9a-fA-F]+)\s+(?P<type>[A-Za-z])\s+(?P<name>\S+)"
    )
    
    for line in output.split("\n"):
        match = symbol_re.match(line.strip())
        if match:
            symbols.append(SymbolInfo(
                name=match.group("name"),
                size=int(match.group("size"), 16),
                type=match.group("type"),
                addr=int(match.group("addr"), 16)
            ))
    
    return symbols


def get_elf_info(elf_path: Path) -> dict:
    """Get ELF file information using readelf."""
    output = run_cmd([
        "/opt/devkitpro/devkitPPC/bin/powerpc-eabi-readelf",
        "-h",
        str(elf_path)
    ])
    
    info = {}
    for line in output.split("\n"):
        if ":" in line:
            key, value = line.split(":", 1)
            info[key.strip()] = value.strip()
    
    return info


def generate_report(elf_path: Path, output_dir: Path) -> ELFReport:
    """Generate comprehensive ELF report."""
    # Get file info
    file_size = elf_path.stat().st_size
    elf_info = get_elf_info(elf_path)
    
    # Parse sections
    sections = parse_sections(elf_path)
    
    # Calculate section sizes
    bss_size = 0
    data_size = 0
    rodata_size = 0
    text_size = 0
    total_section_size = 0
    
    for section in sections:
        total_section_size += section.size
        if section.name == ".bss":
            bss_size = section.size
        elif section.name == ".data":
            data_size = section.size
        elif section.name == ".rodata":
            rodata_size = section.size
        elif section.name == ".text":
            text_size = section.size
    
    # Parse symbols
    symbols = parse_symbols(elf_path)
    
    # Calculate symbol statistics
    symbol_types = {}
    symbol_sizes = []
    for sym in symbols:
        symbol_types[sym.type] = symbol_types.get(sym.type, 0) + 1
        symbol_sizes.append(sym.size)
    
    # Get top 50 largest symbols
    top_symbols = sorted(symbols, key=lambda x: x.size, reverse=True)[:50]
    
    # Build report
    report = ELFReport(
        generated=datetime.now(timezone.utc).isoformat(),
        file_path=str(elf_path),
        file_size=file_size,
        architecture=elf_info.get("Machine", "unknown"),
        sections=[{
            "name": s.name,
            "size": s.size,
            "addr": s.addr,
            "offset": s.offset,
            "align": s.align
        } for s in sections],
        bss_size=bss_size,
        data_size=data_size,
        rodata_size=rodata_size,
        text_size=text_size,
        total_section_size=total_section_size,
        symbol_stats={
            "total_symbols": len(symbols),
            "symbol_type_counts": symbol_types,
            "total_symbol_size": sum(symbol_sizes),
            "max_symbol_size": max(symbol_sizes) if symbol_sizes else 0,
            "min_symbol_size": min(symbol_sizes) if symbol_sizes else 0
        },
        top_symbols=[{
            "name": s.name,
            "size": s.size,
            "type": s.type,
            "addr": s.addr
        } for s in top_symbols]
    )
    
    return report


def write_report(report: ELFReport, output_dir: Path) -> None:
    """Write report to files."""
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # JSON report
    json_path = output_dir / "elf-report.json"
    json_data = asdict(report)
    json_path.write_text(json.dumps(json_data, indent=2) + "\n", encoding="utf-8")
    
    # TSV summary
    tsv_path = output_dir / "section-summary.tsv"
    with tsv_path.open("w", encoding="utf-8") as f:
        f.write("section\tsize_bytes\tsize_hex\taddr\toffset\talign\n")
        for section in report.sections:
            f.write(f"{section['name']}\t{section['size']}\t{section['size']:x}\t"
                   f"{section['addr']:x}\t{section['offset']:x}\t{section['align']}\n")
    
    # TSV symbol report
    symbol_path = output_dir / "symbol-report.tsv"
    with symbol_path.open("w", encoding="utf-8") as f:
        f.write("name\tsize_bytes\tsize_hex\ttype\taddr\n")
        for sym in report.top_symbols:
            f.write(f"{sym['name']}\t{sym['size']}\t{sym['size']:x}\t"
                   f"{sym['type']}\t{sym['addr']:x}\n")
    
    # Markdown summary
    md_path = output_dir / "summary.md"
    with md_path.open("w", encoding="utf-8") as f:
        f.write("# GameCube ELF Memory Report\n\n")
        f.write(f"- Generated: {report.generated}\n")
        f.write(f"- ELF file: `{report.file_path}`\n")
        f.write(f"- File size: {report.file_size:,} bytes ({report.file_size / 1024 / 1024:.2f} MiB)\n")
        f.write(f"- Architecture: {report.architecture}\n\n")
        
        f.write("## Section Sizes\n\n")
        f.write("| Section | Size (bytes) | Size (hex) | Address |\n")
        f.write("|---|---:|---|---|\n")
        for section in report.sections:
            size_kb = section['size'] / 1024
            f.write(f"| {section['name']} | {section['size']:,} | 0x{section['size']:x} | 0x{section['addr']:x} |\n")
        
        f.write("\n## Memory Summary\n\n")
        f.write(f"- **.text** (code): {report.text_size:,} bytes ({report.text_size / 1024:.2f} KiB)\n")
        f.write(f"- **.data** (initialized): {report.data_size:,} bytes ({report.data_size / 1024:.2f} KiB)\n")
        f.write(f"- **.rodata** (read-only): {report.rodata_size:,} bytes ({report.rodata_size / 1024:.2f} KiB)\n")
        f.write(f"- **.bss** (uninitialized): {report.bss_size:,} bytes ({report.bss_size / 1024 / 1024:.2f} MiB)\n")
        f.write(f"- **Total section size**: {report.total_section_size:,} bytes ({report.total_section_size / 1024 / 1024:.2f} MiB)\n\n")
        
        f.write("## Symbol Statistics\n\n")
        f.write(f"- Total symbols: {report.symbol_stats['total_symbols']}\n")
        f.write(f"- Total symbol size: {report.symbol_stats['total_symbol_size']:,} bytes\n")
        f.write(f"- Max symbol size: {report.symbol_stats['max_symbol_size']:,} bytes\n")
        f.write(f"- Min symbol size: {report.symbol_stats['min_symbol_size']:,} bytes\n\n")
        
        f.write("### Symbol Type Distribution\n\n")
        f.write("| Type | Count |\n")
        f.write("|---|---|\n")
        for sym_type, count in sorted(report.symbol_stats['symbol_type_counts'].items()):
            f.write(f"| {sym_type} | {count} |\n")
        
        f.write("\n## Top 50 Largest Symbols\n\n")
        f.write("| Name | Size (bytes) | Type |\n")
        f.write("|---|---:|---|\n")
        for sym in report.top_symbols[:50]:
            f.write(f"| {sym['name']} | {sym['size']:,} | {sym['type']} |\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=Path("OUT/bin/xash"),
                        help="Path to ELF file (default: OUT/bin/xash)")
    parser.add_argument("--output", type=Path, default=None,
                        help="Output directory (default: auto-generated)")
    parser.add_argument("--json", action="store_true",
                        help="Output JSON only")
    args = parser.parse_args()
    
    elf_path = args.elf.resolve()
    if not elf_path.exists():
        print(f"Error: ELF file not found: {elf_path}", file=sys.stderr)
        return 1
    
    output_dir = args.output or Path("elf-reports") / datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    
    print(f"Analyzing ELF: {elf_path}")
    report = generate_report(elf_path, output_dir)
    write_report(report, output_dir)
    
    print(f"\nReport generated in: {output_dir}")
    print(f"  - summary.md")
    print(f"  - elf-report.json")
    print(f"  - section-summary.tsv")
    print(f"  - symbol-report.tsv")
    
    # Print summary to stdout
    print(f"\n### Memory Summary ###")
    print(f"  .text:    {report.text_size / 1024:.2f} KiB")
    print(f"  .data:    {report.data_size / 1024:.2f} KiB")
    print(f"  .rodata:  {report.rodata_size / 1024:.2f} KiB")
    print(f"  .bss:     {report.bss_size / 1024 / 1024:.2f} MiB")
    print(f"  Total:    {report.total_section_size / 1024 / 1024:.2f} MiB")
    
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
