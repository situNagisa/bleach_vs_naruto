#version 450

layout(set = 0, binding = 0) uniform sampler2D sprite_atlas;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
	out_color = texture(sprite_atlas, in_uv);
}
