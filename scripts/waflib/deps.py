#!/usr/bin/env python
# -*- encoding: utf-8 -*-
# Michel Mooij, michel.mooij7@gmail.com

from waflib import Utils
from waflib import Errors


def get_deps(bld, target):
	'''Returns a list of (nested) targets on which this target depends.
	
	:param bld: a *waf* build instance from the top level *wscript*
	:type bld: waflib.Build.BuildContext
	:param target: task name for which the dependencies should be returned
	:type target: str
	:returns: a list of task names on which the given target depends
	'''
	try:
		tgen = bld.get_tgen_by_name(target)
	except Errors.WafError:
		return []
	else:
		uses = Utils.to_list(getattr(tgen, 'use', []))
		deps = uses[:]
		for use in uses:
			deps += get_deps(bld, use)
		return list(set(deps))


def get_tgens(bld, names):
	'''Returns a list of task generators based on the given list of task
	generator names.
	
	:param bld: a *waf* build instance from the top level *wscript*
	:type bld: waflib.Build.BuildContext
	:param names: list of task generator names
	:type names: list of str
	:returns: list of task generators
	'''
	tgens=[]
	for name in names:
		try:
			tgen = bld.get_tgen_by_name(name)
		except Errors.WafError:
			pass
		else:
			tgens.append(tgen)
	return list(set(tgens))


def get_targets(bld):
	'''Returns a list of user specified build targets or None if no specific
	build targets has been selected using the *--targets=* command line option.

	:param bld: a *waf* build instance from the top level *wscript*.
	:type bld: waflib.Build.BuildContext
	:returns: a list of user specified target names (using --targets=x,y,z) or None
	'''
	if bld.targets == '':
		return None
	targets = bld.targets.split(',')
	for target in targets:
		targets += get_deps(bld, target)
	return targets

