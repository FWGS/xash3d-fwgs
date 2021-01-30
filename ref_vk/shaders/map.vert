#version 450

layout(set=0,binding=0) uniform UBO {
	mat4 mvp;
} ubo;

layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexture0;

layout(location=0) out vec3 vPos;
layout(location=1) out vec2 vTexture0;

void main() {
	vPos = aPos.xyz;
	vTexture0 = aTexture0;
	gl_Position = ubo.mvp * vec4(aPos.xyz, 1.);
}
