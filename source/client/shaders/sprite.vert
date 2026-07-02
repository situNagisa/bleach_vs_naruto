#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

layout(push_constant) uniform sprite_push_constants
{
	mat4 view_projection;
	vec4 uv_rect;
} pc;

layout(location = 0) out vec2 out_uv;

void main()
{
	gl_Position = pc.view_projection * vec4(in_position, 1.0);
	out_uv = pc.uv_rect.xy + in_uv * pc.uv_rect.zw;
}
