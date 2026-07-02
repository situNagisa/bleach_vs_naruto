#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

#include <entt/entt.hpp>
#include <exec/async_scope.hpp>
#include <stdexec/execution.hpp>

#include <bvn/platform/platform.h>
#include <bvn/renderer/vulkan_renderer.h>

#include "render_workflow.h"

namespace
{
struct context
{
	context()
	{
		render_workflow._state->renderer = &renderer;
	}

	~context() noexcept
	{
		render_scope.request_stop();
		main_scope.request_stop();
		render_workflow._state->stopping.store(true, ::std::memory_order_release);

		try
		{
			render_workflow.submit();
		}
		catch (...)
		{
		}

		try
		{
			::stdexec::sync_wait(render_scope.on_empty());
		}
		catch (...)
		{
		}

		try
		{
			::stdexec::sync_wait(main_scope.on_empty());
		}
		catch (...)
		{
		}

		try
		{
			::stdexec::sync_wait(render_workflow._state->frame_scope.on_empty());
		}
		catch (...)
		{
		}

		if (renderer.device.handle != VK_NULL_HANDLE)
		{
			(void)::vkDeviceWaitIdle(renderer.device.handle);
		}

		main_scheduler.finish();
		render_workflow._state->_inner_loop.finish();
	}

	context(context const&) = delete;
	auto operator=(context const&) -> context& = delete;

	context(context&&) = delete;
	auto operator=(context&&) -> context& = delete;

		::bvn::platform::event_state events{};
		::std::mutex events_mutex{};
		::std::uint64_t events_revision = 0;
		::std::chrono::milliseconds frame_time{};
		::entt::registry registry{};

		::bvn::platform::sdl_context sdl{};
	::bvn::platform::window window{"bvn m1", 1280, 720};
	::bvn::renderer::vulkan_renderer renderer{window};

	render_workflow render_workflow{};
	::exec::async_scope render_scope{};
	::std::jthread render_thread{[this] { render_workflow._state->_inner_loop.run(); }};

	::stdexec::run_loop main_scheduler{};
	::exec::async_scope main_scope{};
	::std::jthread main_thread{[this] { main_scheduler.run(); }};
};
}
