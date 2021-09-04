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

struct Kusok {
	uint index_offset;
	uint vertex_offset;
	uint triangles;

	// Material
	uint texture;

	float roughness;
	uint material_flags;
	PAD(2)

	vec3 emissive;
	PAD(1)

	vec4 color;
};

#define	kXVkMaterialFlagLighting (1<<0)

#ifndef GLSL
#undef uint
#undef vec3
#undef vec4
#undef TOKENPASTE
#undef TOKENPASTE2
#undef PAD
#endif
