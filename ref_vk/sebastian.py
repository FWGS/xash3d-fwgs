#!/usr/bin/env python3

import json
import argparse
import struct
import copy
from spirv import spv
import sys
import os

# import sys
# print(sys.argv, file=sys.stderr)

parser = argparse.ArgumentParser(description='Build pipeline descriptor')
parser.add_argument('--path', '-p', help='Directory where to look for .spv shader files')
parser.add_argument('--output', '-o', type=argparse.FileType('wb'), help='Compiled pipeline')
parser.add_argument('--depend', '-d', type=argparse.FileType('w'), help='Generate dependency file (json)')
parser.add_argument('pipelines', type=argparse.FileType('r'))
# TODO strip debug OpName OpLine etc
args = parser.parse_args()

spvOp = spv['Op']
spvOpNames = dict()
for name, n in spvOp.items():
	spvOpNames[n] = name

print("cwd", os.path.abspath('.'), file=sys.stderr)

src_dir = os.path.abspath(os.path.dirname(args.pipelines.name))
print("src", src_dir, file=sys.stderr)

# #dst_dir = os.path.abspath(os.path.dirname(args.output.name))
# #print("dst", dst_dir, file=sys.stderr)

shaders_path = os.path.abspath(args.path if args.path else '.')
print("shaders_path", shaders_path, file=sys.stderr)

def removeprefix(s, pre):
	return s[len(pre):] if s.startswith(pre) else s

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
	# TODO move this to Binding
	TYPE_SAMPLER = 0
	TYPE_COMBINED_IMAGE_SAMPLER = 1
	TYPE_SAMPLED_IMAGE = 2
	TYPE_STORAGE_IMAGE = 3
	TYPE_UNIFORM_TEXEL_BUFFER = 4
	TYPE_STORAGE_TEXEL_BUFFER = 5
	TYPE_UNIFORM_BUFFER = 6
	TYPE_STORAGE_BUFFER = 7
	TYPE_UNIFORM_BUFFER_DYNAMIC = 8
	TYPE_STORAGE_BUFFER_DYNAMIC = 9
	TYPE_INPUT_ATTACHMENT = 10
	TYPE_INLINE_UNIFORM_BLOCK = 1000138000
	TYPE_ACCELERATION_STRUCTURE_KHR = 1000150000
	TYPE_MUTABLE_VALVE = 1000351000
	TYPE_SAMPLE_WEIGHT_IMAGE_QCOM = 1000440000
	TYPE_BLOCK_MATCH_IMAGE_QCOM = 1000440001

	def __init__(self):
		self.descriptor_set = None
		self.binding = None
		self.name = None
		self.type_node = None
		self.type = None
		self.storage_class = None
		pass

	def getType(self):
		node = self
		while node:
			#print(f"Checking node {node.name}, {node.storage_class}, {node.type}")
			if node.storage_class == spv['StorageClass']['Uniform']:
				return SpirvNode.TYPE_UNIFORM_BUFFER

			if node.storage_class == spv['StorageClass']['StorageBuffer']:
				return SpirvNode.TYPE_STORAGE_BUFFER

			if node.type:
				return node.type

			node = node.type_node
		raise Exception('Couldn\'t find type for node %s' % self.name)

class SpirvContext:
	def __init__(self, nodes_count):
		self.nodes = [SpirvNode() for i in range(0, nodes_count)]
		#self.bindings = dict()
		pass

	def getNode(self, index):
		return self.nodes[index]

def spvOpName(ctx, args):
	index = args[0]
	name = struct.pack(str(len(args)-1)+'I', *args[1:]).split(b'\x00')[0].decode('utf8')
	ctx.getNode(index).name = name
	#print('Name for', args[0], name, len(name))

def spvOpDecorate(ctx, args):
	node = ctx.getNode(args[0])
	decor = args[1]
	if decor == spv['Decoration']['DescriptorSet']:
		node.descriptor_set = args[2]
	elif decor == spv['Decoration']['Binding']:
		node.binding = args[2]
	#else:
		#print('Decor ', id, decor)

def spvOpVariable(ctx, args):
	type_node = ctx.getNode(args[0])
	node = ctx.getNode(args[1])
	storage_class = args[2]

	node.type_node = type_node
	node.storage_class = storage_class
	#node.op_type = 'OpVariable'
	#print(node.name, "=(var)>", type_node.name, args[0])

def spvOpTypePointer(ctx, args):
	node = ctx.getNode(args[0])
	storage_class = args[1]
	type_node = ctx.getNode(args[2])

	node.type_node = type_node
	node.storage_class = storage_class
	#node.op_type = 'OpTypePointer'
	#print(node.name, "=(ptr)>", type_node.name, args[2])

def spvOpTypeAccelerationStructureKHR(ctx, args):
	node = ctx.getNode(args[0])
	node.type = SpirvNode.TYPE_ACCELERATION_STRUCTURE_KHR

def spvOpTypeImage(ctx, args):
	node = ctx.getNode(args[0])
	sampled_type = args[1]
	dim = args[2]
	depth = args[3]
	arrayed = args[4]
	ms = args[5]
	sampled = args[6]
	image_format = args[7]
	node.type = SpirvNode.TYPE_STORAGE_IMAGE if sampled == 0 or sampled == 2 else SpirvNode.TYPE_COMBINED_IMAGE_SAMPLER # FIXME ?

	node.image_format = image_format
	qualifier = None if len(args) < 9 else args[8]

	#print(f"{args[0]}: Image(type={sampled_type}, dim={dim}, depth={depth}, arrayed={arrayed}, ms={ms}, sampled={sampled}, image_format={image_format}, qualifier={qualifier})")

def spvOpTypeSampledImage(ctx, args):
	node = ctx.getNode(args[0])
	image_type = ctx.getNode(args[1])
	node.type_node = image_type
	node.type = SpirvNode.TYPE_COMBINED_IMAGE_SAMPLER

def spvOpTypeArray(ctx, args):
	node = ctx.getNode(args[0])
	element_type = ctx.getNode(args[1])
	length = args[2]

	node.type_node = element_type
	#print(f"{args[0]}: Array(type={args[1]}, length={length})")

spvOpHandlers = {
	spvOp['OpName']: spvOpName,
	spvOp['OpDecorate']: spvOpDecorate,
	spvOp['OpVariable']: spvOpVariable,
	spvOp['OpTypePointer']: spvOpTypePointer,
	spvOp['OpTypeAccelerationStructureKHR']: spvOpTypeAccelerationStructureKHR,
	spvOp['OpTypeImage']: spvOpTypeImage,
	spvOp['OpTypeSampledImage']: spvOpTypeSampledImage,
	spvOp['OpTypeArray']: spvOpTypeArray,
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

	WRITE_BIT = 0x80000000

	def __init__(self, node):
		self.write = node.name.startswith('out_')
		self.name = removeprefix(node.name, 'out_')
		self.index = node.binding
		self.descriptor_set = node.descriptor_set
		self.stages = 0
		self.type = node.getType()

		#print(f"  {self.name}: ds={self.descriptor_set}, b={self.index}, type={self.type}")

		assert(self.descriptor_set >= 0)
		assert(self.descriptor_set < 255)

		assert(self.index >= 0)
		assert(self.index < 255)

		#TODO: type, count, etc

	def serialize(self, out):
		out.writeString(self.name)
		header = (Binding.WRITE_BIT if self.write else 0) | (self.descriptor_set << 8) | self.index
		out.writeU32(header)
		out.writeU32(self.type)
		out.writeU32(self.stages)

class Shader:
	def __init__(self, name, fullpath):
		self.name = name
		self.__fullpath = fullpath
		self.__raw_data = None
		self.__bindings = None
		#print(name, '=>', len(self.raw_data))

	def __str__(self):
		return self.name
		# ret = ''
		# for index, node in enumerate(self.__spirv.nodes):
		# 	if node.descriptor_set is not None:
		# 		ret += ('[%d:%d] (id=%d) %s\n' % (node.descriptor_set, node.binding, index, node.name))
		# return ret

	def getRawData(self):
		if not self.__raw_data:
			self.__raw_data = open(self.__fullpath, 'rb').read()

		return self.__raw_data

	def getBindings(self):
		if self.__bindings:
			return self.__bindings

		spirv = parseSpirv(self.__raw_data)

		bindings = []
		for node in spirv.nodes:
			if node.binding == None or node.descriptor_set == None:
				continue
			bindings.append(Binding(node))

		self.__bindings = bindings
		return self.__bindings

	def getFilePath(self):
		return self.__fullpath

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

	def load(self, name, stage):
		name = name + self.__suffixes[stage]
		fullpath = os.path.join(shaders_path, name)
		if name in self.__map:
			return self.__shaders[self.__map[name]]

		shader = Shader(name, fullpath)

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
			out.writeBytes(shader.getRawData())

	def getAllFiles(self):
		return [shader.getFilePath() for shader in self.__shaders]

shaders = Shaders()

PIPELINE_COMPUTE = 1
PIPELINE_RAYTRACING = 2
NO_SHADER = 0xffffffff

class Pipeline:
	def __init__(self, name, type_id):
		self.name = name
		self.type = type_id
		self.__shaders = []

	def addShader(self, shader_name, stage):
		shader = shaders.load(shader_name, stage)
		self.__shaders.append((shader, stage))
		return shader

	def __mergeBindings(self):
		bindings = {}
		for shader, stage in self.__shaders:
			for binding in shader.getBindings():
				addr = (binding.descriptor_set, binding.index)
				if not addr in bindings:
					bindings[addr] = copy.deepcopy(binding)

				bindings[addr].stages |= stage
		return bindings

	def serialize(self, out):
		bindings = self.__mergeBindings()
		#print(self.name)
		#for binding in bindings.values():
			#print(f"  {binding.name}: ds={binding.descriptor_set}, b={binding.index}, type={binding.type}, stages={binding.stages:#x}")
		out.writeU32(self.type)
		out.writeString(self.name)
		out.writeArray(bindings.values())

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

if args.depend:
	json.dump([os.path.relpath(file) for file in shaders.getAllFiles()], args.depend)

if args.output:
	writeOutput(args.output, pipelines)
