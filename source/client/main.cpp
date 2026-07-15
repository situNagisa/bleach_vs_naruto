#include <atomic>
#include <chrono>
#include <mutex>
#include <print>
#include <thread>
#include <utility>

#include <stdexec/execution.hpp>

#include <bvn/platform/platform.h>

#include "arena.h"
#include "context.h"
#include "debug_overlay.h"
#include "hero.h"
#include "preview_controller.h"

auto main() -> int
{
	try
	{
		auto stage = arena{};
		auto player = hero{};
		auto overlay = debug_overlay{};
		auto controller = preview_controller{};
		auto game_context = context{};

		game_context.main_scope.spawn(::stdexec::starts_on(game_context.main_scheduler.get_scheduler(), stage.main(game_context)));
		game_context.main_scope.spawn(::stdexec::starts_on(game_context.main_scheduler.get_scheduler(), player.main(game_context)));
		game_context.main_scope.spawn(::stdexec::starts_on(game_context.main_scheduler.get_scheduler(), overlay.main(game_context)));
		game_context.main_scope.spawn(::stdexec::starts_on(game_context.main_scheduler.get_scheduler(), controller.main(game_context)));

		using clock_type = ::std::chrono::steady_clock;
		auto previous_time = clock_type::now();
		auto quit_requested = false;

		while (!quit_requested)
		{
			auto now = clock_type::now();
			auto frame_time = ::std::chrono::duration_cast<::std::chrono::milliseconds>(now - ::std::exchange(previous_time, now));
			auto events = ::bvn::platform::poll_events(game_context.window);
			quit_requested = events.quit_requested;
			if (quit_requested)
			{
				break;
			}

			{
				auto lock = ::std::scoped_lock{game_context.events_mutex};
				game_context.frame_time = frame_time;
				game_context.events = events;
				++game_context.events_revision;
			}

			if (events.resized)
			{
				game_context.renderer._requested_extent = events.drawable_extent;
				game_context.renderer._resize_requested.store(true, ::std::memory_order_release);
			}

			if (events.drawable_extent.width == 0 || events.drawable_extent.height == 0)
			{
				::std::this_thread::sleep_for(::std::chrono::milliseconds{16});
				continue;
			}

			game_context.render_workflow.submit();
		}

		return 0;
	}
	catch (::std::exception const& error)
	{
		::std::println(stderr, "fatal error: {}", error.what());
		return 1;
	}
}
