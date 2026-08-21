#version 450

layout(location = 0) out vec3 color;

const vec2 positions[3] = vec2[](
	vec2(0.0, -0.65),
	vec2(0.65, 0.55),
	vec2(-0.65, 0.55)
);

const vec3 colors[3] = vec3[](
	vec3(1.0, 0.18, 0.12),
	vec3(0.12, 0.85, 0.30),
	vec3(0.16, 0.38, 1.0)
);

void main()
{
	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
	color = colors[gl_VertexIndex];
}
