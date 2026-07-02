#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(push_constant) uniform grid_push_constants
{
	mat4 view_projection;
} pc;

layout(location = 0) out vec3 out_color;

void main()
{
	gl_Position = pc.view_projection * vec4(in_position, 1.0);
	out_color = in_color;
}
