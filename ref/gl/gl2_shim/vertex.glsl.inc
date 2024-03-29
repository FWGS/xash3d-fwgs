"#if VER <= 300\n"
"#define layout(x)\n"
"#endif\n"
"#if VER < 300\n"
"#define in attribute\n"
"#define out varying\n"
"#endif\n"
"\n"
"layout(location = LOC_ATTR_POSITION) in vec3 inPosition;\n"
"#if ATTR_COLOR\n"
"layout(location = LOC_ATTR_COLOR) in vec4 inColor;\n"
"#endif\n"
"#if ATTR_TEXCOORD0\n"
"layout(location = LOC_ATTR_TEXCOORD0) in vec2 inTexCoord0;\n"
"#endif\n"
"#if ATTR_TEXCOORD1\n"
"layout(location = LOC_ATTR_TEXCOORD1) in vec2 inTexCoord1;\n"
"#endif\n"
"\n"
"#if ATTR_NORMAL\n"
"in vec3 inNormal;\n"
"#endif\n"
"#if ATTR_COLOR\n"
"out vec4 vColor;\n"
"#endif\n"
"#if ATTR_TEXCOORD0\n"
"out vec2 vTexCoord0;\n"
"#endif\n"
"#if ATTR_TEXCOORD1\n"
"out vec2 vTexCoord1;\n"
"#endif\n"
"#if ATTR_NORMAL\n"
"out vec3 vNormal;\n"
"#endif\n"
"\n"
"uniform mat4 uMVP;\n"
"\n"
"void main()\n"
"{\n"
"gl_Position = uMVP * vec4(inPosition,1.0);\n"
"#if ATTR_COLOR\n"
"vColor = inColor;\n"
"#endif\n"
"#if ATTR_NORMAL\n"
"vNormal = inNormal;\n"
"#endif\n"
"#if ATTR_TEXCOORD0\n"
"vTexCoord0 = inTexCoord0;\n"
"#endif\n"
"#if ATTR_TEXCOORD1\n"
"vTexCoord1 = inTexCoord1;\n"
"#endif\n"
"}\n"
