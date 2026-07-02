#version 450

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D sprite_texture;

void main()
{
	out_color = texture(sprite_texture, in_uv);
	if (out_color.a <= 0.01)
	{
		discard;
	}
}
