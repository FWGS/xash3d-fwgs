R"(

#define layout(x)
#define in attribute
#define out varying

layout(location = 0) in vec3 inPosition;
#if ATTR_COLOR
layout(location = 1) in vec4 inColor;
#endif
#if ATTR_NORMAL
layout(location = 2) in vec3 inNormal;
#endif
#if ATTR_TEXCOORD0
layout(location = 3) in vec2 inTexCoord0;
#endif
#if ATTR_TEXCOORD1
layout(location = 4) in vec2 inTexCoord1;
#endif

#if ATTR_COLOR
out vec4 vColor;
#endif
#if ATTR_TEXCOORD0
out vec2 vTexCoord0;
#endif
#if ATTR_TEXCOORD1
out vec2 vTexCoord0;
#endif
#if ATTR_NORMAL
out vec3 vNormal;
#endif

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * vec4(inPosition,1.0f);
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
