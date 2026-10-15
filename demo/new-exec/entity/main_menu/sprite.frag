#version 450
layout(set=0,binding=0) uniform sampler2DArray sprite_texture;
layout(push_constant) uniform sprite_parameters
{
	uint frame;
};
layout(location=0) in vec2 frag_uv;
layout(location=0) out vec4 output_color;

void main()
{
	output_color = texture(sprite_texture, vec3(frag_uv, float(frame)));
}
