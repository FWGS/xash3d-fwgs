#!/usr/bin/env python3
"""Generate original, antialiased iOS touch icons without external dependencies.

White pictograms on translucent circular plates keep a consistent silhouette.
Run from any directory; the PNGs are committed so builds need no generator.
"""
import math
from pathlib import Path
import struct
import zlib

SIZE = 128
SCALE = 3
ROOT = Path(__file__).resolve().parents[2] / '3rdparty/extras/ios/touch_ios'


def write_icon(name, strokes=(), rings=(), discs=(), plate=True):
    # Coordinates use a 100 by 100 design canvas.
    pixels = bytearray()
    for row in range(SIZE):
        pixels.append(0)  # PNG filter: none
        for col in range(SIZE):
            samples = []
            for sy in range(SCALE):
                for sx in range(SCALE):
                    x = (col + (sx + .5) / SCALE) * 100 / SIZE
                    y = (row + (sy + .5) / SCALE) * 100 / SIZE
                    radius = math.hypot(x - 50, y - 50)
                    value, alpha = (20, 125) if plate and radius <= 48 else (0, 0)
                    if plate and 45.5 <= radius <= 48:
                        value, alpha = 255, 150
                    ink = any(abs(math.hypot(x-cx, y-cy)-r) <= w/2 for cx, cy, r, w in rings)
                    ink |= any(math.hypot(x-cx, y-cy) <= r for cx, cy, r in discs)
                    for points, width in strokes:
                        for (ax, ay), (bx, by) in zip(points, points[1:]):
                            vx, vy = bx-ax, by-ay
                            t = max(0, min(1, ((x-ax)*vx+(y-ay)*vy)/(vx*vx+vy*vy or 1)))
                            if math.hypot(x-ax-t*vx, y-ay-t*vy) <= width/2:
                                ink = True
                    if ink:
                        value, alpha = 255, 255
                    samples.append((value, alpha))
            a = sum(a for _, a in samples)
            # Average premultiplied samples, then return straight RGBA for PNG.
            v = round(sum(v*a for v, a in samples)/a) if a else 0
            pixels.extend((v, v, v, round(a / (SCALE*SCALE))))

    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind+data))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('!2I5B', SIZE, SIZE, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(pixels, 9)) + chunk(b'IEND', b'')
    (ROOT / (name + '.png')).write_bytes(png)


def arc(cx, cy, radius, start, end):
    return [(cx+radius*math.cos(math.radians(a)), cy+radius*math.sin(math.radians(a)))
            for a in range(start, end+1, 5)]


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    icons = {
        'prev': ([([(58, 30), (38, 50), (58, 70)], 6)], [], []),
        'next': ([([(42, 30), (62, 50), (42, 70)], 6)], [], []),
        'fire': ([([(50, 23), (50, 35)], 4), ([(50, 65), (50, 77)], 4),
                  ([(23, 50), (35, 50)], 4), ([(65, 50), (77, 50)], 4)], [(50, 50, 18, 4)], [(50, 50, 3)]),
        # Match primary fire's reticle, with a prominent II for secondary fire.
        'alt_fire': ([([(50, 23), (50, 32)], 4), ([(50, 68), (50, 77)], 4),
                      ([(23, 50), (32, 50)], 4), ([(68, 50), (77, 50)], 4),
                      ([(44, 41), (44, 59)], 4), ([(56, 41), (56, 59)], 4)], [(50, 50, 19, 3)], []),
        'jump': ([([(50, 69), (50, 30)], 6), ([(34, 46), (50, 30), (66, 46)], 6),
                  ([(30, 73), (70, 73)], 4)], [], []),
        'use': ([([(30, 64), (26, 48), (32, 43), (40, 54), (40, 28), (47, 28), (47, 45),
                  (55, 39), (61, 45), (68, 44), (72, 51), (66, 72), (40, 72), (30, 64)], 4)], [], []),
        # Ammo magazine behind a circular reload arrow.
        'reload': ([([(43, 32), (58, 32), (58, 63), (53, 69), (43, 65), (43, 32)], 3),
                    ([(44, 38), (57, 38)], 2), ([(48, 44), (48, 58)], 2), ([(53, 44), (53, 58)], 2),
                    (arc(50, 50, 27, -65, 220), 4), ([(24, 55), (30, 68), (43, 64)], 4)], [], []),
        'crouch': ([([(51, 42), (43, 54), (62, 59), (55, 72), (72, 72)], 5),
                    ([(46, 49), (30, 57), (25, 47)], 5), ([(43, 54), (34, 72)], 5)], [], [(57, 29, 7)]),
        'menu': ([([(30, y), (70, y)], 5) for y in (34, 50, 66)], [], []),
        'settings': ([], [(50, 50, 19, 5), (50, 50, 7, 4)], []),
        'weapons': ([([(28, 37), (63, 37), (70, 43), (48, 47), (43, 65), (32, 65), (36, 46), (28, 46), (28, 37)], 4)], [], []),
        'flashlight': ([([(28, 40), (54, 40), (62, 32), (62, 68), (54, 60), (28, 60), (28, 40)], 4),
                        ([(70, 50), (79, 50)], 3), ([(69, 33), (76, 27)], 3), ([(69, 67), (76, 73)], 3)], [], []),
        'save': ([([(30, 27), (63, 27), (72, 36), (72, 73), (28, 73), (28, 27), (30, 27)], 4),
                  ([(39, 27), (39, 43), (60, 43), (60, 27)], 3),
                  ([(38, 73), (38, 55), (62, 55), (62, 73)], 3)], [], []),
        'load': ([([(29, 45), (29, 71), (71, 71), (71, 45)], 4), ([(50, 26), (50, 56)], 5),
                  ([(38, 44), (50, 56), (62, 44)], 5)], [], []),
        'scores': ([([(29, 68), (29, 47), (40, 47), (40, 68)], 4), ([(44, 68), (44, 30), (56, 30), (56, 68)], 4),
                    ([(60, 68), (60, 40), (71, 40), (71, 68)], 4)], [], []),
        'chat': ([([(28, 30), (72, 30), (72, 61), (49, 61), (34, 73), (34, 61), (28, 61), (28, 30)], 4)], [], [(39, 46, 2), (50, 46, 2), (61, 46, 2)]),
        'spray': ([([(38, 39), (62, 39), (62, 73), (38, 73), (38, 39)], 4),
                   ([(44, 39), (44, 29), (57, 29), (57, 39)], 3), ([(66, 29), (76, 29)], 3)], [], [(76, 22, 2), (77, 36, 2)]),
        'mic': ([([(35, 48), (35, 54)] + arc(50, 54, 15, 0, 180)[::-1] + [(65, 48)], 4),
                 ([(50, 69), (50, 77)], 4), ([(39, 77), (61, 77)], 4),
                 ([(42, 33), (42, 53)] + arc(50, 53, 8, 0, 180)[::-1] + [(58, 33)] + arc(50, 33, 8, 180, 360), 4)], [], []),
    }
    icons['settings'][0].extend([([(50+18*math.cos(a), 50+18*math.sin(a)),
                                 (50+27*math.cos(a), 50+27*math.sin(a))], 5)
                                for a in [i*math.pi/4 for i in range(8)]])
    write_icon("stick_ring", rings=[(50, 50, 46, 2.5)], plate=False)
    write_icon("stick_thumb", discs=[(50, 50, 48)], plate=False)
    for name, (strokes, rings, discs) in icons.items():
        write_icon(name, strokes, rings, discs)


if __name__ == '__main__':
    main()
