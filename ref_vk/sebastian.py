#!/usr/bin/env python
import json
import argparse
import struct
import traceback
from spirv import spv

parser = argparse.ArgumentParser(description='Build pipeline descriptor')
parser.add_argument('--path', action='append', help='Directory to look for shaders')
parser.add_argument('pipelines', type=argparse.FileType('r'))
# TODO strip debug OpName OpLine etc
args = parser.parse_args()

spvOp = spv['Op']
spvOpNames = dict()
for name, n in spvOp.items():
	spvOpNames[n] = name

class SpirvNode:
	def __init__(self):
		self.descriptor_set = None
		self.binding = None
		self.name = None
		pass

class SpirvContext:
	def __init__(self, nodes_count):
		self.nodes = [SpirvNode() for i in range(0, nodes_count)]
		#self.bindings = dict()
		pass

	def getNode(self, index):
		return self.nodes[index]

	#def bindNode(self, index):
		#if not index in bindings
		#self.bindings[index]


def spvOpHandleName(ctx, args):
	index = args[0]
	name = struct.pack(str(len(args)-1)+'I', *args[1:]).split(b'\x00')[0].decode('utf8')
	ctx.getNode(index).name = name
	#print('Name for', args[0], name, len(name))

def spvOpHandleDecorate(ctx, args):
	node = ctx.getNode(args[0])
	decor = args[1]
	if decor == spv['Decoration']['DescriptorSet']:
		node.descriptor_set = args[2]
	elif decor == spv['Decoration']['Binding']:
		node.binding = args[2]
	#else:
		#print('Decor ', id, decor)

spvOpHandlers = {
	spvOp['OpName']: spvOpHandleName,
	spvOp['OpDecorate']: spvOpHandleDecorate
}

def parseSpirv(raw_data):
	if len(raw_data) % 4 != 0:
		raise Exception('SPIR-V size should be divisible by 4')

	size = len(raw_data) // 4
	if size < 5:
		raise Exception('SPIR-V data is too short')

	data = struct.unpack(str(size) + 'I', raw_data)

	if data[0] != spv['MagicNumber']:
		raise Exception('Unexpected magic ' + str(data[0]))

	nodes_count = data[3]
	ctx = SpirvContext(nodes_count)

	off = 5
	while off < size:
		op = data[off] & 0xffff
		words = data[off] >> 16
		args = data[off+1:off+words]
		if op in spvOpHandlers:
			spvOpHandlers[op](ctx, args)
		#print(spvOpNames[op], args)
		off += words

	for index, node in enumerate(ctx.nodes):
		if node.descriptor_set is not None:
			print('[%d:%d] %s (id=%d)' % (node.descriptor_set, node.binding, node.name, index))

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
