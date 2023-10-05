R"(


#if VER <= 300
#define layout(x)
#endif
#if VER < 300
#define out attribute
#define in varying
#define texture texture2D
#endif
#if VER >= 130
precision mediump float;
#endif
#if ATTR_TEXCOORD0
layout(location = 5) uniform sampler2D uTex0;
#endif
#if ATTR_TEXCOORD1
layout(location = 6) uniform sampler2D uTex1;
#endif
#if FEAT_ALPHA_TEST
uniform float uAlphaTest;
#endif
#if FEAT_FOG
uniform vec4 uFog;
#endif
layout(location = 2) uniform vec4 uColor;
#if ATTR_COLOR
in vec4 vColor;
#endif
#if ATTR_TEXCOORD0
in vec2 vTexCoord0;
#endif
#if ATTR_TEXCOORD1
in vec2 vTexCoord1;
#endif
#if ATTR_NORMAL
in vec2 vNormal;
#endif
#if VER >= 300
out vec4 oFragColor;
#else
#define oFragColor gl_FragColor
#endif
void main()
{
#if ATTR_COLOR
   vec4 c = vColor;
#else
   vec4 c = uColor;
#endif
#if ATTR_TEXCOORD0
    c = c * texture(uTex0, vTexCoord0);
#endif
#if ATTR_TEXCOORD1
    c = c * texture(uTex1, vTexCoord1);
#endif
#if FEAT_ALPHA_TEST
    if(c.a <= uAlphaTest)
        discard;
#endif
#if FEAT_FOG
    float fogDist = gl_FragCoord.z / gl_FragCoord.w;
    float fogRate = clamp(exp(-uFog.w * fogDist), 0.f, 1.f);
    c.rgb = mix(uFog.rgb, c.rgb, fogRate);
#endif
    oFragColor = c;
}
)"
