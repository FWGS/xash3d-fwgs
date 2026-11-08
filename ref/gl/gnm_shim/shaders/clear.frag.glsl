// Clear color for glClear, depth is cleared by the depth block
#version 450

layout(binding = 0) uniform ClearUniforms
{
	vec4 color;
} u;

layout(location = 0) out vec4 oFragColor;

void main()
{
	oFragColor = u.color;
}
