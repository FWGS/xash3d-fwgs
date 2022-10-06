#include "vk_spirv.h"
#include "vk_common.h"

#include <spirv/unified1/spirv.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

//#define L(msg, ...) fprintf(stderr, msg "\n",##__VA_ARGS__)
#define L(msg, ...) gEngine.Con_Reportf(msg "\n",##__VA_ARGS__)
#define LE(msg, ...) gEngine.Con_Printf(S_ERROR msg "\n",##__VA_ARGS__)

#define MALLOC malloc
#define FREE free

#define CHECK(cond) assert(cond)

static const char *opTypeName(uint16_t op) {
	switch(op) {
		case SpvOpVariable: return "SpvOpVariable";
		case SpvOpTypeVoid: return "SpvOpTypeVoid";
		case SpvOpTypeBool: return "SpvOpTypeBool";
		case SpvOpTypeInt: return "SpvOpTypeInt";
		case SpvOpTypeFloat: return "SpvOpTypeFloat";
		case SpvOpTypeVector: return "SpvOpTypeVector";
		case SpvOpTypeMatrix: return "SpvOpTypeMatrix";
		case SpvOpTypeImage: return "SpvOpTypeImage";
		case SpvOpTypeSampler: return "SpvOpTypeSampler";
		case SpvOpTypeSampledImage: return "SpvOpTypeSampledImage";
		case SpvOpTypeArray: return "SpvOpTypeArray";
		case SpvOpTypeRuntimeArray: return "SpvOpTypeRuntimeArray";
		case SpvOpTypeStruct: return "SpvOpTypeStruct";
		case SpvOpTypeOpaque: return "SpvOpTypeOpaque";
		case SpvOpTypePointer: return "SpvOpTypePointer";
		case SpvOpTypeFunction: return "SpvOpTypeFunction";
		case SpvOpTypeEvent: return "SpvOpTypeEvent";
		case SpvOpTypeDeviceEvent: return "SpvOpTypeDeviceEvent";
		case SpvOpTypeReserveId: return "SpvOpTypeReserveId";
		case SpvOpTypeQueue: return "SpvOpTypeQueue";
		case SpvOpTypePipe: return "SpvOpTypePipe";
		case SpvOpTypeForwardPointer: return "SpvOpTypeForwardPointer";
		case SpvOpTypePipeStorage: return "SpvOpTypePipeStorage";
		case SpvOpTypeNamedBarrier: return "SpvOpTypeNamedBarrier";
		case SpvOpTypeRayQueryKHR: return "SpvOpTypeRayQueryKHR";
		case SpvOpTypeAccelerationStructureKHR: return "SpvOpTypeAccelerationStructureKHR";
		case SpvOpTypeCooperativeMatrixNV: return "SpvOpTypeCooperativeMatrixNV";
		default: return "UNKNOWN";
	}
}

static const char *storageClassName(SpvStorageClass storage_class) {
	switch(storage_class) {
		case SpvStorageClassUniformConstant: return "SpvStorageClassUniformConstant";
		case SpvStorageClassInput: return "SpvStorageClassInput";
		case SpvStorageClassUniform: return "SpvStorageClassUniform";
		case SpvStorageClassOutput: return "SpvStorageClassOutput";
		case SpvStorageClassWorkgroup: return "SpvStorageClassWorkgroup";
		case SpvStorageClassCrossWorkgroup: return "SpvStorageClassCrossWorkgroup";
		case SpvStorageClassPrivate: return "SpvStorageClassPrivate";
		case SpvStorageClassFunction: return "SpvStorageClassFunction";
		case SpvStorageClassGeneric: return "SpvStorageClassGeneric";
		case SpvStorageClassPushConstant: return "SpvStorageClassPushConstant";
		case SpvStorageClassAtomicCounter: return "SpvStorageClassAtomicCounter";
		case SpvStorageClassImage: return "SpvStorageClassImage";
		case SpvStorageClassStorageBuffer: return "SpvStorageClassStorageBuffer";
		case SpvStorageClassCallableDataKHR: return "SpvStorageClassCallableDataKHR";
		case SpvStorageClassIncomingCallableDataKHR: return "SpvStorageClassIncomingCallableDataKHR";
		case SpvStorageClassRayPayloadKHR: return "SpvStorageClassRayPayloadKHR";
		case SpvStorageClassHitAttributeKHR: return "SpvStorageClassHitAttributeKHR";
		case SpvStorageClassIncomingRayPayloadKHR: return "SpvStorageClassIncomingRayPayloadKHR";
		case SpvStorageClassShaderRecordBufferKHR: return "SpvStorageClassShaderRecordBufferKHR";
		case SpvStorageClassPhysicalStorageBuffer: return "SpvStorageClassPhysicalStorageBuffer";
		case SpvStorageClassCodeSectionINTEL: return "SpvStorageClassCodeSectionINTEL";
		case SpvStorageClassDeviceOnlyINTEL: return "SpvStorageClassDeviceOnlyINTEL";
		case SpvStorageClassHostOnlyINTEL: return "SpvStorageClassHostOnlyINTEL";
		case SpvStorageClassMax: return "SpvStorageClassMax";
	}
	return "UNKNOWN";
}

static const char *imageFormatName(SpvImageFormat format) {
	switch(format) {
		case SpvImageFormatUnknown: return "SpvImageFormatUnknown";
		case SpvImageFormatRgba32f: return "SpvImageFormatRgba32f";
		case SpvImageFormatRgba16f: return "SpvImageFormatRgba16f";
		case SpvImageFormatR32f: return "SpvImageFormatR32f";
		case SpvImageFormatRgba8: return "SpvImageFormatRgba8";
		case SpvImageFormatRgba8Snorm: return "SpvImageFormatRgba8Snorm";
		case SpvImageFormatRg32f: return "SpvImageFormatRg32f";
		case SpvImageFormatRg16f: return "SpvImageFormatRg16f";
		case SpvImageFormatR11fG11fB10f: return "SpvImageFormatR11fG11fB10f";
		case SpvImageFormatR16f: return "SpvImageFormatR16f";
		case SpvImageFormatRgba16: return "SpvImageFormatRgba16";
		case SpvImageFormatRgb10A2: return "SpvImageFormatRgb10A2";
		case SpvImageFormatRg16: return "SpvImageFormatRg16";
		case SpvImageFormatRg8: return "SpvImageFormatRg8";
		case SpvImageFormatR16: return "SpvImageFormatR16";
		case SpvImageFormatR8: return "SpvImageFormatR8";
		case SpvImageFormatRgba16Snorm: return "SpvImageFormatRgba16Snorm";
		case SpvImageFormatRg16Snorm: return "SpvImageFormatRg16Snorm";
		case SpvImageFormatRg8Snorm: return "SpvImageFormatRg8Snorm";
		case SpvImageFormatR16Snorm: return "SpvImageFormatR16Snorm";
		case SpvImageFormatR8Snorm: return "SpvImageFormatR8Snorm";
		case SpvImageFormatRgba32i: return "SpvImageFormatRgba32i";
		case SpvImageFormatRgba16i: return "SpvImageFormatRgba16i";
		case SpvImageFormatRgba8i: return "SpvImageFormatRgba8i";
		case SpvImageFormatR32i: return "SpvImageFormatR32i";
		case SpvImageFormatRg32i: return "SpvImageFormatRg32i";
		case SpvImageFormatRg16i: return "SpvImageFormatRg16i";
		case SpvImageFormatRg8i: return "SpvImageFormatRg8i";
		case SpvImageFormatR16i: return "SpvImageFormatR16i";
		case SpvImageFormatR8i: return "SpvImageFormatR8i";
		case SpvImageFormatRgba32ui: return "SpvImageFormatRgba32ui";
		case SpvImageFormatRgba16ui: return "SpvImageFormatRgba16ui";
		case SpvImageFormatRgba8ui: return "SpvImageFormatRgba8ui";
		case SpvImageFormatR32ui: return "SpvImageFormatR32ui";
		case SpvImageFormatRgb10a2ui: return "SpvImageFormatRgb10a2ui";
		case SpvImageFormatRg32ui: return "SpvImageFormatRg32ui";
		case SpvImageFormatRg16ui: return "SpvImageFormatRg16ui";
		case SpvImageFormatRg8ui: return "SpvImageFormatRg8ui";
		case SpvImageFormatR16ui: return "SpvImageFormatR16ui";
		case SpvImageFormatR8ui: return "SpvImageFormatR8ui";
		case SpvImageFormatR64ui: return "SpvImageFormatR64ui";
		case SpvImageFormatR64i: return "SpvImageFormatR64i";
		case SpvImageFormatMax: return "SpvImageFormatMax";
	}
	return "UNKNOWN";
}

typedef struct {
	int id;
	const char *name;
	int descriptor_set;
	int binding;

	// TODO: in-out, type, image format, etc
	//uint32_t flags;
} binding_t;

#define MAX_BINDINGS 32
typedef struct node_t {
	SpvOp op;
	int binding;
	const char *name;
	uint32_t type_id;
	uint32_t storage_class;
	uint32_t flags;
} node_t;

typedef struct {
	int nodes_count;
	node_t *nodes;

	int bindings_count;
	binding_t bindings[MAX_BINDINGS];
} context_t;

binding_t *getBinding(context_t *ctx, int id) {
	if(id < 0) {
		LE("id %d < 0", id);
		return NULL;
	}

	if (id >= ctx->nodes_count) {
		LE("id %d > %d", id, ctx->nodes_count);
		return NULL;
	}

	node_t *sid = ctx->nodes + id;

	if (sid->binding < 0) {
		if (ctx->bindings_count >= MAX_BINDINGS) {
			LE("too many bindings %d", MAX_BINDINGS);
			return NULL;
		}

		sid->binding = ctx->bindings_count++;
		ctx->bindings[sid->binding].id = id;
	}

	return ctx->bindings + sid->binding;
}

static qboolean spvParseOp(context_t *ctx, uint16_t op, uint16_t word_count, const uint32_t *args) {
	switch (op) {
		case SpvOpName:
		{
			// FIXME check size, check strlen
			const uint32_t id = args[0];
			const char *name = (const char*)(args + 1);
			//L("OpName(id=%d) => %s", id, name);
			ctx->nodes[id].name = name[0] != '\0' ? name : NULL;
			break;
		}
		case SpvOpMemberName:
		{
			// FIXME check size, check strlen
			const uint32_t id = args[0];
			const uint32_t index = args[1];
			const char *name = (const char*)(args + 2);
			//L("OpMemberName(id=%d) => index=%d %s", id, index, name);
			//ctx->nodes[id].name = name;
			break;
		}
		case SpvOpTypeImage:
		{
			// FIXME check size check strlen
			const uint32_t result_id = args[0];
			const uint32_t type_id = args[1];
			const uint32_t dim = args[2];
			const uint32_t depth = args[3];
			const uint32_t arrayed = args[4];
			const uint32_t ms = args[5];
			const uint32_t sampled = args[6];
			const uint32_t format = args[7];
			node_t *node = ctx->nodes + result_id;
			node->op = op;
			node->type_id = type_id;
			//L("OpTypeImage(id=%d) => type_id=%d dim=%08x depth=%d arrayed=%d ms=%d sampled=%d format=%s(%d)",
				//result_id, type_id, dim, depth, arrayed, ms, sampled,imageFormatName(format), format);
			break;
		}
		case SpvOpTypePointer:
		{
			// FIXME check size
			const uint32_t id = args[0];
			const uint32_t storage_class = args[1];
			const uint32_t type_id = args[2];
			node_t *node = ctx->nodes + id;
			node->op = op;
			node->type_id = type_id;
			node->storage_class = storage_class;
			//L("OpTypePointer(id=%d) => storage_class=%d type_id=%d", id, storage_class, type_id);
			//ctx->nodes[id].name = name;
			break;
		}
		case SpvOpVariable:
		{
			const uint32_t type_id = args[0];
			const uint32_t result_id = args[1];
			const uint32_t storage_class = args[2];
			node_t *node = ctx->nodes + result_id;
			node->op = op;
			node->type_id = type_id;
			node->storage_class = storage_class;
			//L("OpVariable(id=%d) => type_id=%d storage_class=%d", result_id, type_id, storage_class);
			break;
		}
		case SpvOpMemberDecorate:
		{
			const uint32_t id = args[0];
			const uint32_t member_index = args[1];
			const uint32_t decor = args[2];
			//L("OpMemberDecorate(id=%d) => member=%d decor=%d ...", id, member_index, decor);
			break;
		}
		case SpvOpDecorate:
		{
			const uint32_t id = args[0];
			// FIXME check size
			const uint32_t decor = args[1];
			node_t *node = ctx->nodes + id;
			switch (decor) {
				case SpvDecorationDescriptorSet:
				{
					const uint32_t ds = args[2];
					//L("OpDecorate(id=%d) => DescriptorSet = %d ", id, ds);
					binding_t *binding = getBinding(ctx, id);
					if (!binding)
						return false;
					binding->descriptor_set = ds;
					break;
				}

				case SpvDecorationBinding:
				{
					const uint32_t binding = args[2];
					//L("OpDecorate(id=%d) => Binding = %d ", id, binding);
					binding_t *b = getBinding(ctx, id);
					if (!b)
						return false;
					b->binding = binding;
					break;
				}

				case SpvDecorationNonWritable:
				{
					//node->flags &= Flag_NonWritable;
					//L("OpDecorate(id=%d) => NonWriteable", id);
					break;
				}

				case SpvDecorationNonReadable:
				{
					//node->flags &= Flag_NonReadable;
					//L("OpDecorate(id=%d) => NonReadable", id);
					break;
				}

				default:
					break;
					//L("OpDecorate(id=%d) => %d ... ", id, decor);
			}

			break;
		}

		default:
			if (word_count > 1 && (int)args[1] < ctx->nodes_count) {
				const uint32_t id = args[1];
				//L("op=%d word_count=%d guessed dest_id=%d", op, word_count, id);
			} else {
				//L("op=%d word_count=%d", op, word_count);
			}
	}
	return true;
}

qboolean parseHeader(context_t *ctx, const uint32_t *data, int n) {
	const uint32_t magic = data[0];
	//L("magic = %#08x", magic);
	if (magic != SpvMagicNumber)
		return false;

	const uint32_t version = data[1];
	//L("version = %#08x(%d.%d)", version, (version>>16)&0xff, (version>>8)&0xff);

	//L("generator magic = %#08x", data[2]);

	ctx->nodes_count = data[3];
	//L("nodes_count = %d", ctx->nodes_count);

	ctx->nodes = MALLOC(ctx->nodes_count * sizeof(*ctx->nodes));

	for (int i = 0; i < ctx->nodes_count; ++i) {
		ctx->nodes[i].binding = -1;
		ctx->nodes[i].op = SpvOpMax;
		ctx->nodes[i].type_id = -1;
		ctx->nodes[i].name = NULL;
		ctx->nodes[i].storage_class = -1;
		ctx->nodes[i].flags = 0;
	}

	return true;
}

qboolean processBindings(context_t *ctx, vk_spirv_t *out) {
	for (int i = 0; i < ctx->bindings_count; ++i) {
		binding_t *binding = ctx->bindings + i;
		//L("%02d [%d:%d] id=%d name=%s", i, binding->descriptor_set, binding->binding, binding->id, binding->name);
		{
			const node_t *node = ctx->nodes + binding->id;
			for (;;) {
				//L("  op=%s(%d) type_id=%d name=%s", opTypeName(node->op), node->op, node->type_id, node->name ? node->name : "N/A");

				if ((int)node->storage_class >= 0) {
					//L("    storage_class=%s", storageClassName(node->storage_class));
				}

				if (!binding->name && node->name)
					binding->name = node->name;

				//binding->flags |= node->flags;

				if ((int)node->type_id == -1)
					break;
				node = ctx->nodes + node->type_id;
			}
		}
	}

	out->bindings_count = ctx->bindings_count;
	out->bindings = MALLOC(sizeof(vk_binding_t) * out->bindings_count);

	for (int i = 0; i < ctx->bindings_count; ++i) {
		const binding_t *src = ctx->bindings + i;
		vk_binding_t *dst = out->bindings + i;

		dst->binding = src->binding;
		dst->descriptor_set = src->descriptor_set;
		dst->name = strdup(src->name);
	}

	return true;
}

qboolean R_VkSpirvParse(vk_spirv_t *out, const uint32_t *data, int n) {
	if (n < 5) {
		return false;
	}

	context_t ctx = {0};

	if (!parseHeader(&ctx, data, n))
		return false;

	for (int i = 0; i < MAX_BINDINGS; ++i) {
		ctx.bindings[i].id = -1;
		ctx.bindings[i].descriptor_set = -1;
		ctx.bindings[i].binding = -1;
	}

	qboolean ret = false;
	for (size_t i = 5; i < n; ++i) {
		const uint16_t op_code = data[i] & SpvOpCodeMask;
		const uint16_t word_count = data[i] >> SpvWordCountShift;

		if (word_count > (n-i))
			goto cleanup;

		if (!spvParseOp(&ctx, op_code, word_count, data + i + 1))
			goto cleanup;

		i += word_count-1;
	}

	if (!processBindings(&ctx, out))
		goto cleanup;

	ret = true;

cleanup:
	if (ctx.nodes)
		FREE(ctx.nodes);
	return ret;
}

void R_VkSpirvFree(vk_spirv_t *spirv) {
	for (int i = 0; i < spirv->bindings_count; ++i) {
		vk_binding_t *bind = spirv->bindings + i;
		if (bind->name)
			FREE(bind->name);
	}

	FREE(spirv->bindings);
	memset(spirv, 0, sizeof(*spirv));
}
