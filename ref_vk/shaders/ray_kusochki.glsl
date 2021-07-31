#extension GL_EXT_shader_16bit_storage : require

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
	vec2 lm_tc;
};

layout(std430, binding = 3, set = 0) readonly buffer Kusochki { Kusok kusochki[]; };
layout(std430, binding = 4, set = 0) readonly buffer Indices { uint16_t indices[]; };
layout(std430, binding = 5, set = 0) readonly buffer Vertices { Vertex vertices[]; };