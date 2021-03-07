#version 450

layout (constant_id = 0) const float alpha_test_threshold = 0.;
layout (constant_id = 1) const uint max_dlights = 1;

layout(set=1,binding=0) uniform sampler2D sTexture0;
layout(set=2,binding=0) uniform sampler2D sLightmap;

struct Light {
	vec4 pos_r;
	vec4 color;
};

layout(set=3,binding=0) uniform UBO {
	uint num_lights;
	Light lights[max_dlights];
} ubo;

layout(location=0) in vec3 vPos;
layout(location=1) in vec3 vNormal;
layout(location=2) in vec2 vTexture0;
layout(location=3) in vec2 vLightmapUV;
layout(location=4) in vec4 vColor;

layout(location=0) out vec4 outColor;

void main() {
	outColor = vec4(0.);
	const vec4 baseColor = texture(sTexture0, vTexture0) * vColor;

	if (baseColor.a < alpha_test_threshold)
		discard;

	outColor.a = baseColor.a;

	outColor.rgb += baseColor.rgb * texture(sLightmap, vLightmapUV).rgb;

	for (uint i = 0; i < ubo.num_lights; ++i) {
		// TODO use pos_r.w as radius
		const vec3 light_dir = ubo.lights[i].pos_r.xyz - vPos;
		outColor.rgb += baseColor.rgb * max(0., dot(light_dir, vNormal)) / length(light_dir);
	}

	//outColor.rgb = vNormal * .5 + .5;
}
