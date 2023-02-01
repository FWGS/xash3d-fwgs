#ifndef RAY_KUSOCHKI_GLSL_INCLUDED
#define RAY_KUSOCHKI_GLSL_INCLUDED
#extension GL_EXT_shader_16bit_storage : require
//#extension GL_EXT_shader_8bit_storage : require

#define GLSL
#include "ray_interop.h"
#undef GLSL

struct Vertex {
	vec3 pos;
	vec3 prev_pos;
	vec3 normal;
	vec3 tangent;
	vec2 gl_tc;
	vec2 _unused_lm_tc;

	//float padding;
	//uint8_t color[4];
	uint _unused_color_u8_4;
};

layout(std430, binding = 3, set = 0) readonly buffer Kusochki { Kusok a[]; } kusochki;
layout(std430, binding = 4, set = 0) readonly buffer Indices { uint16_t a[]; } indices;
layout(std430, binding = 5, set = 0) readonly buffer Vertices { Vertex a[]; } vertices;

Kusok getKusok(uint index) { return kusochki.a[index]; }
uint16_t getIndex(uint index) { return indices.a[index]; }
Vertex getVertex(uint index) { return vertices.a[index]; }

#endif //ifndef RAY_KUSOCHKI_GLSL_INCLUDED
