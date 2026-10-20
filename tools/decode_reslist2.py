#!/usr/bin/env python3
"""
Decodifica o svc_resourcelist gravado por reslist_raw.bin e tenta achar o
layout real (larguras de campo) por tentativa e erro, exigindo que TODOS os
nomes seguidos de tamanho/fechamento facam sentido.
"""
import sys

PFX = [b'sprites/', b'maps/', b'models/', b'sound/', b'overviews/',
       b'gfx/', b'events/', b'dlls/', b'resource/']

data = open(sys.argv[1] if len(sys.argv) > 1 else 'reslist_raw.bin', 'rb').read()
MAXB = len(data) * 8


def rd(bit, n):
    v = 0
    for k in range(n):
        b = bit + k
        if b >= MAXB:
            return None
        v = (v << 1) | ((data[b >> 3] >> (7 - (b & 7))) & 1)
    return v


def rname(bit, limit=64):
    o = bytearray()
    p = bit
    while len(o) < limit and p + 8 <= MAXB:
        c = rd(p, 8)
        if c is None or c < 32 or c > 126:
            break
        o.append(c)
        p += 8
    return bytes(o)


def valid(s):
    return 6 <= len(s) < 64 and any(s.startswith(p) for p in PFX)


def find_names(start=0):
    """todos os offsets de bit onde comeca um nome plausivel"""
    out = []
    bit = start
    while bit < MAXB - 48:
        s = rname(bit)
        if valid(s):
            out.append((bit, s))
            bit += (len(s) + 1) * 8
        else:
            bit += 1
    return out


names = find_names()
print("nomes encontrados: %d" % len(names))
for b, s in names[:5]:
    print("  bit=%6d  byte=%7.2f  %s" % (b, b / 8.0, s.decode()))

# mede os gaps entre nomes consecutivos
print("\ngaps (bits) entre fim do nome+1 e inicio do proximo:")
prev_end = None
gaps = []
for b, s in names:
    end = b + (len(s) + 1) * 8
    if prev_end is not None:
        gaps.append(b - prev_end)
    prev_end = end
from collections import Counter
print("  ", Counter(gaps).most_common(10))

# tenta descobrir o layout da cauda usando o gap mais comum
if names:
    b0, s0 = names[0]
    e0 = b0 + (len(s0) + 1) * 8
    b1, s1 = names[1]
    tail_bits = b1 - e0
    print("\ncauda entre 1o e 2o nome: %d bits" % tail_bits)
    print("  bits:", ''.join(str(rd(e0 + k, 1)) for k in range(tail_bits)))
    print("  valor:", rd(e0, tail_bits))
    for w in (12, 24, 3, 1, 8, 32):
        print("    %2d bits = %d" % (w, rd(e0, w)))
