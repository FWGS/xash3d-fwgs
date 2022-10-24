#!/usr/bin/env python
import json
import argparse
import struct
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
		print(json.dumps(result, sort_keys=False, indent=4))
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

class Shaders:
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

	def load(self, name):
		if name in self.__map:
			return self.__shaders[self.__map[name]]

		file = Shaders.__loadShaderFile(name);
		shader = Shader(name, file)

		index = len(self.__shaders)
		self.__shaders.append(shader)
		self.__map[name] = index

		return shader

	def getIndex(self, name):
		return self.__map[name]

	def serialize(self, out):
		out.writeU32(len(self.__shaders))
		for shader in self.__shaders:
			out.writeString(shader.name)
			out.writeBytes(shader.raw_data)

	def serializeIndex(self, out, shader):
		out.write(struct.pack('I', self.getIndex(shader.name)))

shaders = Shaders()


PIPELINE_COMPUTE = 1
PIPELINE_RAYTRACING = 2
NO_SHADER = 0xffffffff

class PipelineRayTracing:
	def __loadHit(hit):
		ret = dict()
		suffixes = {'closest': '.rchit.spv', 'any': '.rahit.spv'}
		for k, v in hit.items():
			ret[k] = shaders.load(v + suffixes[k])
		return ret

	def __init__(self, name, desc):
		self.type = PIPELINE_RAYTRACING
		self.name = name
		self.rgen = shaders.load(desc['rgen'] + '.rgen.spv')
		self.miss = [] if not 'miss' in desc else [shaders.load(s + '.rmiss.spv') for s in desc['miss']]
		self.hit = [] if not 'hit' in desc else [PipelineRayTracing.__loadHit(hit) for hit in desc['hit']]

	def serialize(self, out):
		shaders.serializeIndex(out, self.rgen)

		out.write(struct.pack('I', len(self.miss)))
		for shader in self.miss:
			shaders.serializeIndex(out, shader)

		out.write(struct.pack('I', len(self.hit)))
		for hit in self.hit:
			if 'closest' in hit:
				shaders.serializeIndex(out, hit['closest'])
			else:
				out.write(struct.pack('I', NO_SHADER))

			if 'any' in hit:
				shaders.serializeIndex(out, hit['any'])
			else:
				out.write(struct.pack('I', NO_SHADER))

class PipelineCompute:
	def __init__(self, name, desc):
		self.type = PIPELINE_COMPUTE
		self.name = name
		self.comp = shaders.load(desc['comp'] + '.comp.spv')

	def serialize(self, out):
		shaders.serializeIndex(out, self.comp)

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

	out.writeU32(len(pipelines))
	for name, pipeline in pipelines.items():
		out.writeU32(pipeline.type)
		out.writeString(pipeline.name)
		pipeline.serialize(out)

pipelines = loadPipelines()

if args.output:
	writeOutput(args.output, pipelines)
