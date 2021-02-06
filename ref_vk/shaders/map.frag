#version 450

layout (constant_id = 0) const float alpha_test_threshold = 0.;

layout(set=1,binding=0) uniform sampler2D sTexture0;
layout(set=2,binding=0) uniform sampler2D sLightmap;

layout(location=0) in vec3 vPos;
layout(location=1) in vec2 vTexture0;
layout(location=2) in vec2 vLightmapUV;
layout(location=3) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = texture(sTexture0, vTexture0) * vColor * texture(sLightmap, vLightmapUV);
	if (outColor.a < alpha_test_threshold)
		discard;
}
