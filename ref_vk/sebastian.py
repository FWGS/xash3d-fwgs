#!/usr/bin/env python
import json
import argparse
import struct
import traceback

parser = argparse.ArgumentParser(description='Build pipeline descriptor')
parser.add_argument('pipelines', type=argparse.FileType('r'))
parser.add_argument('--path', nargs='*', help='Directory to look for shaders')
args = parser.parse_args()

def parseSpirv(raw_data):
	if len(raw_data) % 4 != 0:
		raise Exception('SPIR-V size should be divisible by 4')

	size = len(raw_data) // 4
	data = struct.unpack(str(size) + 'I', raw_data)

	print(data[0:5])

class Shader:
	def __init__(self, name, filename):
		self.name = name
		self.__raw_data = open(filename, 'rb').read()
		print(name, '=>', len(self.__raw_data))

		try:
			parseSpirv(self.__raw_data)
		except:
			traceback.print_exc()

def doLoadShader(name):
	try:
		return Shader(name, name)
	except:
		pass

	if args.path:
		for path in args.path:
			try:
				return Shader(name, path + '/' + name)
			except:
				pass

	raise Exception('Cannot load shader ' + name)

shaders = dict()
def loadShader(name):
	if name in shaders:
		return shaders[name]

	shader = doLoadShader(name)
	shaders[name] = shader
	return shader

class PipelineRayTracing:
	def __init__(self, name, desc):
		self.name = name
		self.rgen = loadShader(desc['rgen'] + '.rgen.spv')
		self.miss = [] if not 'miss' in desc else [loadShader(s + '.rmiss.spv') for s in desc['miss']]

		def loadHit(hit):
			ret = dict()
			suffixes = {'closest': '.rchit.spv', 'any': '.rahit.spv'}
			for k, v in hit.items():
				ret[k] = loadShader(v + suffixes[k])
			return ret

		self.hit = [] if not 'hit' in desc else [loadHit(hit) for hit in desc['hit']]

class PipelineCompute:
	def __init__(self, name, desc):
		self.name = name
		self.comp = loadShader(desc['comp'] + '.comp.spv')

def parsePipeline(pipelines, name, desc):
	if 'inherit' in desc:
		inherit = pipelines[desc['inherit']]
		for k, v in inherit.items():
			if not k in desc:
				desc[k] = v
	if 'rgen' in desc:
		return PipelineRayTracing(name, desc)
	elif 'comp' in desc:
		return PipelineCompute(name, desc)

def loadPipelines():
	pipelines_desc = json.load(args.pipelines)
	pipelines = dict()
	for k, v in pipelines_desc.items():
		if 'template' in v and v['template']:
			continue
		pipelines[k] = parsePipeline(pipelines_desc, k, v)
	return pipelines

pipelines = loadPipelines()
