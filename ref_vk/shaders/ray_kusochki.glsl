#extension GL_EXT_shader_16bit_storage : require
//#extension GL_EXT_shader_8bit_storage : require

// Shared with native
#define	kXVkMaterialFlagEmissive (1<<0)
#define	kXVkMaterialFlagDiffuse (1<<1) // means: compute lighting
#define	kXVkMaterialFlagAdditive (1<<2)
#define	kXVkMaterialFlagReflective (1<<3)
#define	kXVkMaterialFlagRefractive (1<<4)
#define	kXVkMaterialFlagAlphaTest (1<<5)

// TODO?
// DoLighting
// PassThrough ??? reflective vs refractive + additive

struct Kusok {
	uint index_offset;
	uint vertex_offset;
	uint triangles;

	// Material
	uint texture;
	float roughness;
	uint material_flags;
};

struct Vertex {
	vec3 pos;
	vec3 normal;
	vec2 gl_tc;
	vec2 _unused_lm_tc;

	//float padding;
	//uint8_t color[4];
	uint _unused_color_u8_4;
};

layout(std430, binding = 3, set = 0) readonly buffer Kusochki { Kusok kusochki[]; };
layout(std430, binding = 4, set = 0) readonly buffer Indices { uint16_t indices[]; };
layout(std430, binding = 5, set = 0) readonly buffer Vertices { Vertex vertices[]; };
