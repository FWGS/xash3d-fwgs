R"(

#if VER <= 300
#define layout(x)
#endif
#if VER < 300
#define in attribute
#define out varying
#endif

layout(location = LOC_ATTR_POSITION) in vec3 inPosition;
#if ATTR_COLOR
layout(location = LOC_ATTR_COLOR) in vec4 inColor;
#endif
#if ATTR_TEXCOORD0
layout(location = LOC_ATTR_TEXCOORD0) in vec2 inTexCoord0;
#endif
#if ATTR_TEXCOORD1
layout(location = LOC_ATTR_TEXCOORD1) in vec2 inTexCoord1;
#endif

#if ATTR_NORMAL
in vec3 inNormal;
#endif
#if ATTR_COLOR
out vec4 vColor;
#endif
#if ATTR_TEXCOORD0
out vec2 vTexCoord0;
#endif
#if ATTR_TEXCOORD1
out vec2 vTexCoord1;
#endif
#if ATTR_NORMAL
out vec3 vNormal;
#endif

uniform mat4 uMVP;

void main()
{
	gl_Position = uMVP * vec4(inPosition,1.0f);
#if ATTR_COLOR
    vColor = inColor;
#endif
#if ATTR_NORMAL
    vNormal = inNormal;
#endif
#if ATTR_TEXCOORD0
    vTexCoord0 = inTexCoord0;
#endif
#if ATTR_TEXCOORD1
    vTexCoord1 = inTexCoord1;
#endif
}
)"
