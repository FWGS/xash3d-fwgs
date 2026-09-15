#!/usr/bin/env python3

import struct
import sys

HLBSP_VERSION = 30 # only HL
HEADER_LUMPS  = 15
LUMP_ENTITIES = 0
LUMP_PLANES   = 1
IDEXTRAHEADER = b'XASH'

# copies the check from the engine
def looks_like_entities(data, ofs, length):
	return b'"classname"' in data[ofs:ofs + length]

def detect(data):
	version, = struct.unpack_from('<i', data, 0)
	if version != HLBSP_VERSION:
		sys.exit('unknown bsp version (expected %d, got %d)' % (HLBSP_VERSION, version))

	lumps = [struct.unpack_from('<ii', data, 4 + i * 8) for i in range(HEADER_LUMPS)]

	# engine skips bshift detection for maps with extra header
	extident_ofs = 4 + HEADER_LUMPS * 8
	if data[extident_ofs:extident_ofs + 4] == IDEXTRAHEADER:
		return 'xash'

	if looks_like_entities(data, *lumps[LUMP_ENTITIES]):
		return 'hl'

	if looks_like_entities(data, *lumps[LUMP_PLANES]):
		return 'bshift'

	sys.exit('no entities lump found in lumps %d or %d' % (LUMP_ENTITIES, LUMP_PLANES))

def swap(data):
	ent_ofs   = 4 + LUMP_ENTITIES * 8
	plane_ofs = 4 + LUMP_PLANES * 8
	ent   = data[ent_ofs:ent_ofs + 8]
	plane = data[plane_ofs:plane_ofs + 8]
	data[ent_ofs:ent_ofs + 8]     = plane
	data[plane_ofs:plane_ofs + 8] = ent

def load(path):
	with open(path, 'rb') as f:
		data = bytearray(f.read())

	current = detect(data)
	return data, current

def main():
	if len(sys.argv) == 2: # to test the detection
		unused, current = load(sys.argv[1])
		print(current)
		return 0

	if len(sys.argv) != 4 or sys.argv[1] not in ('hl', 'bshift'):
		sys.exit('usage: %s hl|bshift <in.bsp> <out.bsp>' % sys.argv[0])

	target, path, out = sys.argv[1], sys.argv[2], sys.argv[3]
	data, current = load(path)
	print('input map is in \'%s\' format' % current)

	if current == 'xash':
		sys.exit('bsp30ext map, refusing to swap')
		return 1

	if current == target:
		print('already %s, refusing to swap' % target)
		return 0 # not an error technically

	swap(data)

	with open(out, 'wb') as f:
		f.write(data)

	print('converted %s to \'%s\' format' % (out, target))
	return 0

if __name__ == '__main__':
	sys.exit(main())
