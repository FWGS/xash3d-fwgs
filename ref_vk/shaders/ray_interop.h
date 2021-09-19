// Common definitions for both shaders and native code

#ifndef GLSL
#define uint uint32_t
#define vec3 vec3_t
#define vec4 vec4_t
#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#define PAD(x) float TOKENPASTE2(pad_, __LINE__)[x];
#else
#define PAD(x)
#endif

#define GEOMETRY_BIT_ANY 0x01
#define GEOMETRY_BIT_OPAQUE 0x02

struct Kusok {
	uint index_offset;
	uint vertex_offset;
	uint triangles;

	// Material
	uint texture;

	vec4 color;

	vec3 emissive;
	//PAD(1)

	float roughness;
};

struct PointLight {
	vec4 position;
	vec4 color;
};

#ifndef GLSL
#undef uint
#undef vec3
#undef vec4
#undef TOKENPASTE
#undef TOKENPASTE2
#undef PAD
#endif
