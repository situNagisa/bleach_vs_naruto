#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

namespace bvn::render
{
	struct camera
	{
	public:
		glm::mat4 view = glm::mat4{1.0f};
		glm::mat4 projection = glm::mat4{1.0f};
	};

	struct sprite_instance
	{
	public:
		glm::vec3 position = {};
		bool facing_right = true;
		std::uint64_t animation_tick = 0;
	};

	struct render_scene
	{
	public:
		camera view_camera;
		std::vector<sprite_instance> sprites;
	};
}
