#!/usr/bin/env python3
# Generates pkg.gp4 project for PkgTool.Core from all files in the current directory.
# create-gp4 from the toolchain has a hardcoded directory list, so we make our own.
# Usage: make-gp4.py CONTENT_ID
import os, sys, time

content_id = sys.argv[1]
files = []
dirs = set()

for root, dnames, fnames in os.walk('.'):
	for f in fnames:
		path = os.path.relpath(os.path.join(root, f), '.')
		if path.endswith('.gp4'):
			continue
		files.append(path)
		d = os.path.dirname(path)
		while d:
			dirs.add(d)
			d = os.path.dirname(d)

def write_dirs(out, parent, depth):
	children = sorted(d for d in dirs if os.path.dirname(d) == parent)
	for d in children:
		name = os.path.basename(d)
		indent = '\t' * depth
		if any(os.path.dirname(x) == d for x in dirs):
			out.append('%s<dir targ_name="%s">' % (indent, name))
			write_dirs(out, d, depth + 1)
			out.append('%s</dir>' % indent)
		else:
			out.append('%s<dir targ_name="%s" />' % (indent, name))

out = [
	'<?xml version="1.0"?>',
	'<psproject xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" fmt="gp4" version="1000">',
	'\t<volume>',
	'\t\t<volume_type>pkg_ps4_app</volume_type>',
	'\t\t<volume_id>PS4VOLUME</volume_id>',
	'\t\t<volume_ts>%s</volume_ts>' % time.strftime('%Y-%m-%d %H:%M:%S'),
	'\t\t<package content_id="%s" passcode="00000000000000000000000000000000" storage_type="digital50" app_type="full" />' % content_id,
	'\t\t<chunk_info chunk_count="1" scenario_count="1">',
	'\t\t\t<chunks>',
	'\t\t\t\t<chunk id="0" layer_no="0" label="Chunk #0" />',
	'\t\t\t</chunks>',
	'\t\t\t<scenarios default_id="0">',
	'\t\t\t\t<scenario id="0" type="sp" initial_chunk_count="1" label="Scenario #0">0</scenario>',
	'\t\t\t</scenarios>',
	'\t\t</chunk_info>',
	'\t</volume>',
	'\t<files img_no="0">',
]
for f in sorted(files):
	out.append('\t\t<file targ_path="%s" orig_path="%s" />' % (f, f))
out.append('\t</files>')
out.append('\t<rootdir>')
write_dirs(out, '', 2)
out.append('\t</rootdir>')
out.append('</psproject>')

with open('pkg.gp4', 'w') as fp:
	fp.write('\n'.join(out) + '\n')
