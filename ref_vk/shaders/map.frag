#version 450

layout(location=0) in vec3 vPos;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = vec4(abs(fract(vPos/10.)), 1.);
}
