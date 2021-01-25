#version 450

layout(set=0,binding=0) uniform UBO {
	mat4 mvp;
} ubo;

layout(location=0) in vec3 aPos;

layout(location=0) out vec3 vPos;

void main() {
	vPos = aPos.xyz;
	gl_Position = ubo.mvp * vec4(aPos.xyz, 1.);
}
