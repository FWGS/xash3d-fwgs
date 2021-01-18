#version 450

layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUv;
layout(location=2) in vec4 aColor;

layout(location=0) out vec2 vUv;
layout(location=1) out vec4 vColor;

void main() {
	gl_Position = vec4(aPos, 0., 1.);
	vUv = aUv;
	vColor = aColor;
}
