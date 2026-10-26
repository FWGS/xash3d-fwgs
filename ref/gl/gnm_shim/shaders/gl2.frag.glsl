// Fragment shader of gl2_shim (ref/gl/gl2_shim/fragment.glsl.inc) for GNM.
// Built with defines ATTR_COLOR, ATTR_TEXCOORD0, ATTR_TEXCOORD1,
// FEAT_ALPHA_TEST, FEAT_FOG (0 or 1).
#version 450

layout(binding = 0) uniform FSUniforms
{
	vec4 color;
	vec4 fog;   // rgb - color, a - density
	vec4 alpha; // x - alpha test reference
} u;

#if ATTR_TEXCOORD0
layout(binding = 1) uniform sampler2D uTex0;
#endif
#if ATTR_TEXCOORD1
layout(binding = 2) uniform sampler2D uTex1;
#endif

#if ATTR_COLOR
layout(location = 0) in vec4 vColor;
#endif
#if ATTR_TEXCOORD0
layout(location = ATTR_COLOR) in vec2 vTexCoord0;
#endif
#if ATTR_TEXCOORD1
layout(location = ATTR_COLOR + ATTR_TEXCOORD0) in vec2 vTexCoord1;
#endif

layout(location = 0) out vec4 oFragColor;

void main()
{
#if ATTR_COLOR
	vec4 c = vColor;
#else
	vec4 c = u.color;
#endif
#if ATTR_TEXCOORD0
	c = c * texture( uTex0, vTexCoord0 );
#endif
#if ATTR_TEXCOORD1
	c = c * texture( uTex1, vTexCoord1 );
#endif
#if FEAT_ALPHA_TEST
	if( c.a <= u.alpha.x )
		discard;
#endif
#if FEAT_FOG
	float fogDist = gl_FragCoord.z / gl_FragCoord.w;
	float fogRate = clamp( exp( -u.fog.w * fogDist ), 0.0, 1.0 );
	c.rgb = mix( u.fog.rgb, c.rgb, fogRate );
#endif
	oFragColor = c;
}
