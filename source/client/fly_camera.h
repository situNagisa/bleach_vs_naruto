#pragma once

#include <glm/glm.hpp>

namespace
{
	enum class control_mode
	{
		hero,
		camera,
	};

	// Free-look ("Minecraft creative") camera: WASD planar, Space/LShift vertical, mouse-look.
	struct fly_camera
	{
		::glm::vec3 position = {0.0f, 9.0f, -10.0f};
		float yaw = 0.0f;     // radians about +Y; 0 looks toward +Z
		float pitch = -0.6f;  // radians; negative tilts down
		float move_speed = 12.0f;
		float look_sensitivity = 0.0025f;
	};
}
