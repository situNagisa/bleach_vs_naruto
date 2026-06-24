#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <thread>
#include <print>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include <bvn/compute/compute.h>
#include <bvn/platform/platform.h>
#include <bvn/render/imgui_layer.h>
#include <bvn/render/render_scene.h>
#include <bvn/render/vulkan_renderer.h>
#include <bvn/rhi/vulkan_context.h>
#include <bvn/sim/sim.h>

namespace
{
	
	auto to_rhi_extent(bvn::platform::window_extent extent) -> bvn::rhi::surface_extent
	{
		return bvn::rhi::surface_extent
		{
			.width = extent.width,
			.height = extent.height,
		};
	}

	auto make_camera(bvn::rhi::surface_extent extent) -> bvn::render::camera
	{
		auto aspect = extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / static_cast<float>(extent.height);
		auto projection = glm::perspective(glm::radians(35.0f), aspect, 0.1f, 100.0f);
		projection[1][1] *= -1.0f;

		return bvn::render::camera
		{
			.view = glm::lookAt(glm::vec3{0.0f, 12.0f, -12.0f}, glm::vec3{0.0f, 2.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}),
			.projection = projection,
		};
	}

	auto interpolate(bvn::sim::preview_unit const& previous, bvn::sim::preview_unit const& current, double alpha) -> bvn::sim::preview_unit
	{
		auto t = static_cast<float>(alpha);
		return bvn::sim::preview_unit
		{
			.position = previous.position * (1.0f - t) + current.position * t,
			.facing_right = current.facing_right,
		};
	}

}

auto main() -> int
{
	try
	{
		auto sdl = bvn::platform::sdl_context{};
		auto window = bvn::platform::window{"bvn m0", 1280, 720};

		auto rhi = bvn::rhi::vulkan_context
		{
			bvn::rhi::vulkan_context_create_info
			{
				.instance_extensions = window.required_vulkan_extensions(),
				.create_surface = [&](VkInstance instance)
				{
					return window.create_vulkan_surface(instance);
				},
				.initial_extent = to_rhi_extent(window.drawable_extent()),
				.enable_validation = true,
			}
		};

		auto overlay = bvn::render::imgui_layer{window, rhi};
		auto renderer = bvn::render::vulkan_renderer{rhi};
		auto simulation = bvn::sim::preview_simulation{};
		auto sim_clock = bvn::sim::fixed_step_clock{};
		sim_clock.set_fps(30);
		auto snapshots = bvn::sim::snapshot_buffer{};
		snapshots.publish(bvn::sim::capture(simulation));

		using clock_type = ::std::chrono::steady_clock;
		auto previous_time = clock_type::now();
		auto quit_requested = false;
		auto frame_time = ::std::chrono::milliseconds{};

		while (!quit_requested)
		{
			auto now = clock_type::now();
			frame_time = ::std::chrono::duration_cast<::std::chrono::milliseconds>(now - ::std::exchange(previous_time, now));

			auto events = bvn::platform::poll_events(window, [&](SDL_Event const& event)
			{
				overlay.process_event(event);
			});

			quit_requested = events.quit_requested;

			if (events.resized)
			{
				rhi.resize(to_rhi_extent(events.drawable_extent));
				overlay.after_swapchain_recreated(rhi);
				renderer.after_swapchain_recreated();
			}

			if (events.drawable_extent.width == 0 || events.drawable_extent.height == 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds{16});
				continue;
			}

			sim_clock.add_frame_time(frame_time);

			while (sim_clock.should_step())
			{
				bvn::sim::step(simulation);

				sim_clock.consume_step();
				snapshots.publish(bvn::sim::capture(simulation));
			}

			auto unit = interpolate(snapshots.previous().unit, snapshots.current().unit, sim_clock.interpolation_alpha());
			auto scene = bvn::render::render_scene
			{
				.view_camera = make_camera(to_rhi_extent(events.drawable_extent)),
				.sprites =
				{
					bvn::render::sprite_instance
					{
						.position = unit.position,
						.facing_right = unit.facing_right,
						.animation_tick = snapshots.current().tick,
					},
				},
			};

			overlay.new_frame();
			overlay.build_debug_overlay(bvn::render::debug_overlay_state
			{
				.frame_time = frame_time,
				.sim_alpha = sim_clock.interpolation_alpha(),
				.tick = snapshots.current().tick,
			});
			overlay.render();

			renderer.draw(scene, overlay);
		}

		rhi.wait_idle();
		return 0;
	}
	catch (::std::exception const& error)
	{
		::std::println(stderr, "fatal error: {}", error.what());
		return 1;
	}
}
