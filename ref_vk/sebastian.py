#!/usr/bin/env python
import json
import argparse
import struct
import copy
from spirv import spv

parser = argparse.ArgumentParser(description='Build pipeline descriptor')
parser.add_argument('--path', action='append', help='Directory to look for shaders')
parser.add_argument('--output', '-o', type=argparse.FileType('wb'), help='Compiled pipeline')
parser.add_argument('pipelines', type=argparse.FileType('r'))
# TODO strip debug OpName OpLine etc
args = parser.parse_args()

spvOp = spv['Op']
spvOpNames = dict()
for name, n in spvOp.items():
	spvOpNames[n] = name

# remove comment lines and fix comma
def prepareJSON(path):
	raw_json = buffer = result = ""
	onecomment = blockcomment = 0
	for char in path.read():
		if (len(buffer) > 1):
			buffer = buffer[1:]
		buffer += char
		if buffer == "*/":
			blockcomment = 0
			raw_json = raw_json[:-1]
		elif blockcomment:
			continue
		elif buffer == "/*":
			blockcomment = 1
		elif char == "\n" or char == "\r":
			buffer = ""
			onecomment = 0
		elif char == "\t" or char == " " or onecomment:
			continue
		elif buffer == "//":
			raw_json = raw_json[:-1]
			onecomment = 1
		elif buffer != "":
			raw_json += char
	raw_json = raw_json.replace(",]","]")
	raw_json = raw_json.replace(",}","}")
	try:
		result = json.loads(raw_json)
		#print(json.dumps(result, sort_keys=False, indent=4))
	except json.decoder.JSONDecodeError as exp:
		print("Decoding JSON has failed")
		print(raw_json)
		raise
	return result

class Serializer:
	def __init__(self, file):
		self.file = file

	def write(self, v):
		self.file.write(v)

	def writeU32(self, v):
		self.write(struct.pack('I', v))

	def writeBytes(self, v):
		self.writeU32(len(v))
		self.write(v)

	def writeString(self, v):
		bs = v.encode('utf-8') + b'\x00'
		rem = len(bs) % 4
		if rem != 0:
			bs += b'\x00' * (4 - rem)
		self.writeBytes(bs)

	def writeArray(self, v):
		self.writeU32(len(v))
		for i in v:
			if isinstance(i, int):
				self.writeU32(i)
			else:
				i.serialize(self)

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

	return ctx

class Binding:
	STAGE_VERTEX_BIT = 0x00000001
	STAGE_TESSELLATION_CONTROL_BIT = 0x00000002
	STAGE_TESSELLATION_EVALUATION_BIT = 0x00000004
	STAGE_GEOMETRY_BIT = 0x00000008
	STAGE_FRAGMENT_BIT = 0x00000010
	STAGE_COMPUTE_BIT = 0x00000020
	STAGE_ALL_GRAPHICS = 0x0000001F
	STAGE_ALL = 0x7FFFFFFF
	STAGE_RAYGEN_BIT_KHR = 0x00000100
	STAGE_ANY_HIT_BIT_KHR = 0x00000200
	STAGE_CLOSEST_HIT_BIT_KHR = 0x00000400
	STAGE_MISS_BIT_KHR = 0x00000800
	STAGE_INTERSECTION_BIT_KHR = 0x00001000
	STAGE_CALLABLE_BIT_KHR = 0x00002000
	STAGE_TASK_BIT_NV = 0x00000040
	STAGE_MESH_BIT_NV = 0x00000080
	STAGE_SUBPASS_SHADING_BIT_HUAWEI = 0x00004000

	def __init__(self, name, descriptor_set, index, stages):
		self.name = name
		self.index = index
		self.descriptor_set = descriptor_set
		self.stages = stages
		#TODO: type, count, etc

	def serialize(self, out):
		out.writeString(self.name)
		out.writeU32(self.descriptor_set)
		out.writeU32(self.index)
		out.writeU32(self.stages)

class Shader:
	def __init__(self, name, file):
		self.name = name
		self.raw_data = file
		print(name, '=>', len(self.raw_data))
		self.spirv = parseSpirv(self.raw_data)

	def __str__(self):
		ret = ''
		for index, node in enumerate(self.spirv.nodes):
			if node.descriptor_set is not None:
				ret += ('[%d:%d] (id=%d) %s\n' % (node.descriptor_set, node.binding, index, node.name))
		return ret

	def getBindings(self):
		ret = []
		for node in self.spirv.nodes:
			if node.binding == None or node.descriptor_set == None:
				continue
			ret.append(Binding(node.name, node.descriptor_set, node.binding, 0))
		return ret

class Shaders:
	__suffixes = {
		Binding.STAGE_COMPUTE_BIT: '.comp.spv',
		Binding.STAGE_RAYGEN_BIT_KHR: '.rgen.spv',
		Binding.STAGE_ANY_HIT_BIT_KHR: '.rahit.spv',
		Binding.STAGE_CLOSEST_HIT_BIT_KHR: '.rchit.spv',
		Binding.STAGE_MISS_BIT_KHR: '.rmiss.spv'
	}

	def __init__(self):
		self.__map = dict()
		self.__shaders = []

	def __loadShaderFile(name):
		try:
			return open(name, 'rb').read()
		except:
			pass

		if args.path:
			for path in args.path:
				try:
					return open(path + '/' + name, 'rb').read()
				except:
					pass

		raise Exception('Cannot load shader ' + name)

	def load(self, name, stage):
		name = name + self.__suffixes[stage]
		if name in self.__map:
			return self.__shaders[self.__map[name]]

		file = Shaders.__loadShaderFile(name);
		shader = Shader(name, file)

		index = len(self.__shaders)
		self.__shaders.append(shader)
		self.__map[name] = index

		return shader

	def getIndex(self, shader):
		return self.__map[shader.name]

	def serialize(self, out):
		out.writeU32(len(self.__shaders))
		for shader in self.__shaders:
			out.writeString(shader.name)
			out.writeBytes(shader.raw_data)

shaders = Shaders()

PIPELINE_COMPUTE = 1
PIPELINE_RAYTRACING = 2
NO_SHADER = 0xffffffff

class Pipeline:
	def __init__(self, name, type_id):
		self.name = name
		self.type = type_id
		self.__bindings = {}

	def addShader(self, shader_name, stage):
		shader = shaders.load(shader_name, stage)
		for binding in shader.getBindings():
			addr = (binding.descriptor_set, binding.index)
			if not addr in self.__bindings:
				self.__bindings[addr] = copy.deepcopy(binding)

			self.__bindings[addr].stages |= stage

		return shader

	def serialize(self, out):
		#print(self.__bindings)
		out.writeU32(self.type)
		out.writeString(self.name)
		out.writeArray(self.__bindings.values())

class PipelineRayTracing(Pipeline):
	__hit2stage = {
		'closest': Binding.STAGE_CLOSEST_HIT_BIT_KHR,
		'any': Binding.STAGE_ANY_HIT_BIT_KHR,
	}
	def __init__(self, name, desc):
		super().__init__(name, PIPELINE_RAYTRACING)
		self.rgen = self.addShader(desc['rgen'], Binding.STAGE_RAYGEN_BIT_KHR)
		self.miss = [] if not 'miss' in desc else [self.addShader(s, Binding.STAGE_MISS_BIT_KHR) for s in desc['miss']]
		self.hit = [] if not 'hit' in desc else [self.__loadHit(hit) for hit in desc['hit']]

	def serialize(self, out):
		super().serialize(out)
		out.writeU32(shaders.getIndex(self.rgen))
		out.writeArray([shaders.getIndex(s) for s in self.miss])

		out.writeU32(len(self.hit))
		for hit in self.hit:
			out.writeU32(shaders.getIndex(hit['closest']) if 'closest' in hit else NO_SHADER)
			out.writeU32(shaders.getIndex(hit['any']) if 'any' in hit else NO_SHADER)

	def __loadHit(self, hit):
		ret = dict()
		for k, v in hit.items():
			ret[k] = self.addShader(v, self.__hit2stage[k])
		return ret

class PipelineCompute(Pipeline):
	def __init__(self, name, desc):
		super().__init__(name, PIPELINE_COMPUTE)
		self.comp = self.addShader(desc['comp'], Binding.STAGE_COMPUTE_BIT)

	def serialize(self, out):
		super().serialize(out)
		out.writeU32(shaders.getIndex(self.comp))

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
	pipelines_desc = prepareJSON(args.pipelines)
	pipelines = dict()
	for k, v in pipelines_desc.items():
		if 'template' in v and v['template']:
			continue
		pipelines[k] = parsePipeline(pipelines_desc, k, v)
	return pipelines

def writeOutput(file, pipelines):
	MAGIC = bytearray([ord(c) for c in 'MEAT'])
	out = Serializer(file)
	out.write(MAGIC)
	shaders.serialize(out)
	out.writeArray(pipelines.values())

pipelines = loadPipelines()

if args.output:
	writeOutput(args.output, pipelines)
