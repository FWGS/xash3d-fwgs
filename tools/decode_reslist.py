#!/usr/bin/env python3
"""Decodifica o svc_resourcelist de um buffer.dat gravado pela engine.

Uso:
    python3 tools/decode_reslist.py caminho/buffer.dat [cmd_byte]
"""
import sys

PREFIXES = (b"sprites/", b"maps/", b"models/", b"sound/", b"overviews/",
            b"gfx/", b"events/", b"dlls/", b"resource/")


class Bits:
    def __init__(self, data, bit):
        self.d = data
        self.bit = bit

    def ub(self, n):
        v = 0
        for _ in range(n):
            if self.bit >= len(self.d) * 8:
                return None
            v = (v << 1) | ((self.d[self.bit >> 3] >> (7 - (self.bit & 7))) & 1)
            self.bit += 1
        return v

    def string(self, maxlen=128):
        out = bytearray()
        while len(out) < maxlen:
            c = self.ub(8)
            if c is None or c == 0:
                break
            if c < 32 or c > 126:
                return None
            out.append(c)
        return bytes(out)


def try_layout(data, start, count_bits, type_bits, index_bits, size_bits,
               flags_bits, reserved_bits, max_entries=10):
    b = Bits(data, start)
    total = b.ub(count_bits)
    if total is None or not (1 <= total <= 4096):
        return 0, []

    names = []
    for _ in range(min(total, max_entries)):
        if b.ub(type_bits) is None:
            return 0, names
        name = b.string()
        if not name or len(name) < 4:
            return 0, names
        if not any(name.startswith(p) for p in PREFIXES):
            return 0, names
        names.append(name)
        if b.ub(index_bits) is None:
            return 0, names
        if b.ub(size_bits) is None:
            return 0, names
        if b.ub(flags_bits) is None:
            return 0, names
        if reserved_bits:
            flag = b.ub(1)
            if flag:
                for _ in range(64):
                    if b.ub(1) is None:
                        return 0, names
    return len(names), names


def main():
    path = sys.argv[1]
    cmd = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x2B
    data = open(path, "rb").read()
    print("arquivo: %s (%d bytes)" % (path, len(data)))

    starts = [i for i in range(len(data)) if data[i] == cmd]
    print("posicoes de 0x%02x: %s" % (cmd, starts[:8]))

    layouts = []
    for count_bits in (11, 12, 13):
        for type_bits in (3, 4):
            for reserved in (0, 1):
                layouts.append((count_bits, type_bits, 12, 24, 3, reserved))

    best = None
    for pos in starts[:4]:
        for start in range(pos * 8, pos * 8 + 32):
            for lay in layouts:
                n, names = try_layout(data, start, *lay)
                if n >= 4 and (best is None or n > best[0]):
                    best = (n, start, lay, names)

    if not best:
        print("NENHUM layout valido.")
        return 1

    n, start, lay, names = best
    print("\nMELHOR LAYOUT")
    print("  start_bit     = %d (byte %d, bit %d)" % (start, start // 8, start % 8))
    print("  count_bits    = %d" % lay[0])
    print("  type_bits     = %d" % lay[1])
    print("  index_bits    = %d" % lay[2])
    print("  size_bits     = %d" % lay[3])
    print("  flags_bits    = %d" % lay[4])
    print("  reserved_xash = %d" % lay[5])
    print("  entradas      = %d" % n)
    for x in names:
        print("   -", x.decode())
    return 0


if __name__ == "__main__":
    sys.exit(main())
