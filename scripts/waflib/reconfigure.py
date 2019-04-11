#!/usr/bin/env python
# encoding: utf-8
# Copyright (c) 2019 mittorn

'''
Reconfigure

Store/load configuration user input

Usage:
		def options(opt):
			opt.load('reconfigure')

		def configure(conf):
			conf.load('reconfigure')

		./waf configure --reconfigure
'''

from waflib import Configure, Logs, Options, Utils, ConfigSet
import os

import optparse
STORE_PATH = 'build/configuration.py'

def options(opt):
	opt.add_option('--rebuild-cache', dest='rebuild_cache', default=False, action='store_true', help='load previous configuration')
	opt.add_option('--reconfigure', dest='reconfigure', default=False, action='store_true', help='load and update configuration')

def configure(conf):
	store_data = ConfigSet.ConfigSet()
	options = vars(conf.options)
	environ = conf.environ
	if conf.options.reconfigure or conf.options.rebuild_cache:
		store_data.load(STORE_PATH)
		if conf.options.reconfigure:
			for o in options:
				if options[o]: store_data['OPTIONS'][o] = options[o]
			store_data['ENVIRON'].update(environ)
			store_data.store(STORE_PATH)
		conf.environ = store_data['ENVIRON']
		conf.options = optparse.Values(store_data['OPTIONS'])
	else:
	    store_data['OPTIONS'] = vars(conf.options)
	    store_data['ENVIRON'] = conf.environ
	    store_data.store(STORE_PATH)
	    