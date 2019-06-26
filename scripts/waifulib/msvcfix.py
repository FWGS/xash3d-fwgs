#!/usr/bin/env
from waflib.TaskGen import feature, after_method

@feature('c', 'cxx')
@after_method('apply_flags_msvc')
def make_pdb_unique(self):
	for t in self.compiled_tasks:
		pdb_unique_cflag = '/Fd' + t.outputs[0].change_ext('.pdb').abspath()
		t.env.append_value('CFLAGS', pdb_unique_cflag)
		t.env.append_value('CXXFLAGS', pdb_unique_cflag)
