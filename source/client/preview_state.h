#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

#include <glm/glm.hpp>

#include <bvn/renderer/camera.h>

namespace
{
	struct preview_unit_state
	{
		::glm::vec3 position = {};
		::glm::vec3 velocity = {};
		bool facing_right = true;
		::std::uint64_t simulation_tick = 0;
	};

	struct preview_state
	{
		::std::mutex data_mutex;
		::bvn::renderer::camera view_camera;
		preview_unit_state hero_unit;
		::std::atomic_bool control_toggle_requested{false};
		::std::atomic_bool keyboard_captured{false};
		bool control_is_camera = false;
		::std::uint64_t tick = 0;
		long long frame_time_ms = 0;
		double sim_alpha = 0.0;
		float hero_speed = 0.0f;
	};
}
