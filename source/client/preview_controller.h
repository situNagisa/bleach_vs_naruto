#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexec/execution.hpp>
#include <nagisa/concurrency/coroutine.h>

#include <SDL3/SDL_scancode.h>

#include <bvn/gameplay/entity.h>
#include <bvn/renderer/camera.h>
#include <bvn/sim/sim.h>

#include "context.h"
#include "fly_camera.h"
#include "preview_state.h"

namespace
{
	struct preview_controller
	{
		auto main(context& game_context) -> ::bvn::gameplay::task
		{
			if (auto existing = game_context.registry.ctx().find<preview_state>(); existing != nullptr)
			{
				preview = existing;
			}
			else
			{
				preview = &game_context.registry.ctx().emplace<preview_state>();
			}
			sim_clock.step_seconds = ::std::chrono::milliseconds{1'000 / 30};
			snapshots.publish(::bvn::sim::capture(simulation));

			auto env = co_await ::nagisa::concurrency::environment();
			auto scheduler = ::stdexec::get_scheduler(env);
			auto stop = ::stdexec::get_stop_token(env);

			while (!stop.stop_requested())
			{
				co_await ::stdexec::schedule(scheduler);

				if (stop.stop_requested())
				{
					break;
				}

				auto events = ::bvn::platform::event_state{};
				auto frame_time = ::std::chrono::milliseconds{};
				auto events_revision = ::std::uint64_t{};

				{
					auto lock = ::std::lock_guard{game_context.events_mutex};
					events = game_context.events;
					frame_time = game_context.frame_time;
					events_revision = game_context.events_revision;
				}

				if (events_revision == observed_events_revision)
				{
					::std::this_thread::sleep_for(::std::chrono::milliseconds{1});
					continue;
				}

				observed_events_revision = events_revision;

				auto dt_seconds = static_cast<float>(frame_time.count()) / 1000.0f;

				//+ control-mode toggle
				{
					auto toggle_down = events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_F1)];

					if (toggle_down && !toggle_was_down)
					{
						control = control == control_mode::camera ? control_mode::hero : control_mode::camera;
						::bvn::platform::relative_mouse_mode(game_context.window, control == control_mode::camera);
					}

					toggle_was_down = toggle_down;
				}

				if (preview->control_toggle_requested.exchange(false, ::std::memory_order_acq_rel))
				{
					control = control == control_mode::camera ? control_mode::hero : control_mode::camera;
					::bvn::platform::relative_mouse_mode(game_context.window, control == control_mode::camera);
				}

				auto move_intent = ::glm::vec3{0.0f};
				auto camera = ::bvn::renderer::camera{};

				//+ route input to camera or hero
				{
					auto allow_input = !preview->keyboard_captured.load(::std::memory_order_acquire);
					auto forward = ::glm::vec3{};
					auto ground_forward = ::glm::vec3{};
					auto ground_right = ::glm::vec3{};

					if (control == control_mode::camera)
					{
						view_camera.yaw -= events.input.mouse_dx * view_camera.look_sensitivity;
						view_camera.pitch -= events.input.mouse_dy * view_camera.look_sensitivity;
						view_camera.pitch = ::std::clamp(view_camera.pitch, -1.55334f, 1.55334f);
					}

					forward =
					{
						::std::cos(view_camera.pitch) * ::std::sin(view_camera.yaw),
						::std::sin(view_camera.pitch),
						::std::cos(view_camera.pitch) * ::std::cos(view_camera.yaw),
					};
					ground_forward = {forward.x, 0.0f, forward.z};

					if (auto length = ::glm::length(ground_forward); length > 1.0e-4f)
					{
						ground_forward /= length;
					}
					else
					{
						ground_forward = {0.0f, 0.0f, 1.0f};
					}

					ground_right = {ground_forward.z, 0.0f, -ground_forward.x};

					if (control == control_mode::camera && allow_input)
					{
						auto forward_axis = (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_W)] ? 1.0f : 0.0f)
							- (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_S)] ? 1.0f : 0.0f);
						auto right_axis = (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_D)] ? 1.0f : 0.0f)
							- (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_A)] ? 1.0f : 0.0f);
						auto vertical_axis = (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_SPACE)] ? 1.0f : 0.0f)
							- (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_LSHIFT)] ? 1.0f : 0.0f);
						auto planar = ground_forward * forward_axis - ground_right * right_axis;

						if (auto length = ::glm::length(planar); length > 1.0e-4f)
						{
							view_camera.position += (planar / length) * view_camera.move_speed * dt_seconds;
						}

						view_camera.position.y += vertical_axis * view_camera.move_speed * dt_seconds;
					}
					else if (allow_input)
					{
						auto forward_axis = (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_W)] ? 1.0f : 0.0f)
							- (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_S)] ? 1.0f : 0.0f);
						auto right_axis = (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_D)] ? 1.0f : 0.0f)
							- (events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_A)] ? 1.0f : 0.0f);
						auto intent = ground_forward * forward_axis - ground_right * right_axis;

						if (auto length = ::glm::length(intent); length > 1.0e-4f)
						{
							auto sprint = events.input.keys[static_cast<::std::size_t>(::SDL_SCANCODE_LSHIFT)];
							move_intent = (intent / length) * (sprint ? 1.0f : 0.55f);
						}
					}

					auto aspect = events.drawable_extent.height == 0 ? 1.0f : static_cast<float>(events.drawable_extent.width) / static_cast<float>(events.drawable_extent.height);
					auto projection = ::glm::perspective(::glm::radians(35.0f), aspect <= 0.0f ? 1.0f : aspect, 0.1f, 100.0f);
					projection[1][1] *= -1.0f;
					camera =
					{
						.view = ::glm::lookAt(view_camera.position, view_camera.position + forward, ::glm::vec3{0.0f, 1.0f, 0.0f}),
						.projection = projection,
					};
				}

				sim_clock.add_frame_time(frame_time);

				while (sim_clock.should_step())
				{
					::bvn::sim::step(simulation, move_intent);
					sim_clock.consume_step();
					snapshots.publish(::bvn::sim::capture(simulation));
				}

				auto unit = ::bvn::sim::preview_unit{};

				//+ interpolate simulation snapshot
				{
					auto t = static_cast<float>(sim_clock.interpolation_alpha());
					auto&& previous = snapshots.previous().unit;
					auto&& current = snapshots.current().unit;
					unit =
					{
						.position = previous.position * (1.0f - t) + current.position * t,
						.velocity = current.velocity,
						.facing_right = current.facing_right,
					};
				}

				{
					auto lock = ::std::lock_guard{preview->data_mutex};
					preview->view_camera = camera;
					preview->hero_unit =
					{
						.position = unit.position,
						.velocity = unit.velocity,
						.facing_right = unit.facing_right,
						.simulation_tick = snapshots.current().tick,
					};
					preview->control_is_camera = control == control_mode::camera;
					preview->tick = snapshots.current().tick;
					preview->frame_time_ms = static_cast<long long>(frame_time.count());
					preview->sim_alpha = sim_clock.interpolation_alpha();
					preview->hero_speed = ::glm::length(snapshots.current().unit.velocity);
				}
			}
		}

		preview_state* preview = nullptr;
		::std::uint64_t observed_events_revision = 0;
		control_mode control = control_mode::hero;
		fly_camera view_camera;
		bool toggle_was_down = false;
		::bvn::sim::preview_simulation simulation;
		::bvn::sim::fixed_step_clock sim_clock;
		::bvn::sim::snapshot_buffer snapshots;
	};
}
