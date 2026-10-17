// Vertex shader of gl2_shim (ref/gl/gl2_shim/vertex.glsl.inc) for GNM.
// Built with defines ATTR_COLOR, ATTR_TEXCOORD0, ATTR_TEXCOORD1 (0 or 1).
//
// opengnm-psbc can't fetch vertex attributes without a fetch shader,
// so attributes are pulled from raw buffers: one buffer per attribute,
// layout described by attr[] in the uniform buffer (see gnm_gl.c).
#version 450

#define ATTR_FLOAT 0
#define ATTR_UBYTE 1 // normalized unsigned bytes

layout(binding = 0) uniform VSUniforms
{
	mat4 mvp;
	ivec4 attr[4]; // per attribute: offset in bytes, stride, components, type
	ivec4 misc;    // x - base vertex
} u;

layout(std430, binding = 1) readonly buffer PosBuf { uint d[]; } bpos;
#if ATTR_COLOR
layout(std430, binding = 2) readonly buffer ColorBuf { uint d[]; } bcolor;
#endif
#if ATTR_TEXCOORD0
layout(std430, binding = 3) readonly buffer Tc0Buf { uint d[]; } btc0;
#endif
#if ATTR_TEXCOORD1
layout(std430, binding = 4) readonly buffer Tc1Buf { uint d[]; } btc1;
#endif

#if ATTR_COLOR
layout(location = 0) out vec4 vColor;
#endif
#if ATTR_TEXCOORD0
layout(location = ATTR_COLOR) out vec2 vTexCoord0;
#endif
#if ATTR_TEXCOORD1
layout(location = ATTR_COLOR + ATTR_TEXCOORD0) out vec2 vTexCoord1;
#endif

out gl_PerVertex
{
	vec4 gl_Position;
};

// GLSL can't pass buffers to functions, so fetch is a macro
#define FETCH( B, A, V, OUT ) \
{ \
	int ofs = u.attr[A].x + ( V ) * u.attr[A].y; \
	int n = u.attr[A].z; \
	int w = ofs >> 2; \
	OUT = vec4( 0.0, 0.0, 0.0, 1.0 ); \
	if( u.attr[A].w == ATTR_FLOAT ) \
	{ \
		OUT.x = uintBitsToFloat( B.d[w] ); \
		if( n > 1 ) OUT.y = uintBitsToFloat( B.d[w + 1] ); \
		if( n > 2 ) OUT.z = uintBitsToFloat( B.d[w + 2] ); \
		if( n > 3 ) OUT.w = uintBitsToFloat( B.d[w + 3] ); \
	} \
	else \
	{ \
		uint shift = uint( ofs & 3 ) * 8u; \
		uint bytes = B.d[w]; \
		if( shift != 0u ) \
			bytes = ( bytes >> shift ) | ( B.d[w + 1] << ( 32u - shift )); \
		vec4 c = unpackUnorm4x8( bytes ); \
		OUT.x = c.x; \
		if( n > 1 ) OUT.y = c.y; \
		if( n > 2 ) OUT.z = c.z; \
		if( n > 3 ) OUT.w = c.w; \
	} \
}

void main()
{
	int v = gl_VertexIndex + u.misc.x;
	vec4 pos;

	FETCH( bpos, 0, v, pos );
	gl_Position = u.mvp * vec4( pos.xyz, 1.0 );

	// OpenGL clip space depth is -w..w, GNM uses 0..w
	gl_Position.z = ( gl_Position.z + gl_Position.w ) * 0.5;

#if ATTR_COLOR
	FETCH( bcolor, 1, v, vColor );
#endif
#if ATTR_TEXCOORD0
	vec4 tc0;
	FETCH( btc0, 2, v, tc0 );
	vTexCoord0 = tc0.xy;
#endif
#if ATTR_TEXCOORD1
	vec4 tc1;
	FETCH( btc1, 3, v, tc1 );
	vTexCoord1 = tc1.xy;
#endif
}
