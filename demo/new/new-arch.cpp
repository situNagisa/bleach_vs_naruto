#include <cstdint>
#include <cstdio>
#include <exception>
#include <print>
#include <memory>
#include <optional>
#include <variant>
#include <chrono>
#include <thread>

#include <boost/sml.hpp>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <nagisa/concurrency/when_all_range.h>
#include <nagisa/concurrency/coroutine.h>
#include <nagisa/concurrency/simple_task.h>
#include <nagisa/concurrency/_manual_lifetime.h>

#include <bvn/platform/sdl_context.h>

#include "./entity/render.h"
#include "./entity/input.h"
#include "./entity/main_menu.h"
#include "./entity/setting_scene.h"
#include "./entity/fighting_scene.h"
#include "./framework/frame_context.h"
#include "./entity/setting_scene/settings.h"

int main() try
{
	auto sdl = ::bvn::platform::sdl_context{};
	auto pool = ::exec::static_thread_pool{ 4 };
	auto stop_source = ::stdexec::inplace_stop_source{};

	struct exit_scene {};

	struct entitites_t
	{
		renderer draw{};
		input controls{ draw.window };
		union scene_t
		{
			exit_scene exit;
			main_menu menu;
			fighting_scene fighting;

			~scene_t() noexcept {}
		} scene{};
		setting_scene settings{draw};
	} entitites{};
	entitites.draw.set_fullscreen(entitites.settings.preferences().fullscreen);

	auto frame_index = ::std::uint64_t{};

	struct exit {};
	struct open_settings {};
	struct start_fighting {};
	struct back_to_menu {};

	struct machine
	{
		auto operator()() const noexcept{
			using ::boost::sml::state;
			using ::boost::sml::event;
			using ::boost::sml::on_entry;
			using ::boost::sml::on_exit;
			using ::boost::sml::_;
			return ::boost::sml::make_transition_table(
				*state<main_menu> + on_entry<_> / [] (entitites_t& entitites) { ::std::construct_at(&entitites.scene.menu, entitites.draw); }
				, state<main_menu> + on_exit<_> / [] (entitites_t& entitites) noexcept { ::std::destroy_at(&entitites.scene.menu); }
				, state<main_menu> + event<exit> = state<exit_scene>
				, state<main_menu> + event<open_settings> = state<setting_scene>
				, state<main_menu> + event<start_fighting> = state<fighting_scene>

				, state<setting_scene> + event<back_to_menu> = state<main_menu>
				, state<setting_scene> + event<exit> = state<exit_scene>

				, state<fighting_scene> + on_entry<_> / [] (entitites_t& entitites) { ::std::construct_at(&entitites.scene.fighting, entitites.controls, entitites.draw); }
				, state<fighting_scene> + on_exit<_> / [] (entitites_t& entitites) noexcept { ::std::destroy_at(&entitites.scene.fighting); }
				, state<fighting_scene> + event<back_to_menu> = state<main_menu>
				, state<fighting_scene> + event<exit> = state<exit_scene>
				);
		}
	};

	::boost::sml::sm<machine> sm{ entitites };

	for (;;)
	{
		auto const frame_started = ::std::chrono::steady_clock::now();
		{
			frame_context context{
				.index = frame_index,
				.scheduler = pool.get_scheduler(),
				.stop_token = stop_source.get_token(),
			};

			if (sm.is(::boost::sml::state<main_menu>))
			{
				auto task = ::stdexec::when_all(
					entitites.draw.begin()
					, entitites.controls.poll() | ::stdexec::let_value(::std::bind_front(&main_menu::update, &entitites.scene.menu))
					)
					| ::stdexec::let_value([&](renderer::lease frame_slot, main_menu::state state, main_menu::delta delta)
						{
							return
								entitites.scene.menu.record(*frame_slot.token(), ::std::move(state))
								| ::stdexec::let_value([&](::std::vector<VkCommandBuffer> commands, auto...)
									{
										return entitites.draw.end(*frame_slot.token(), commands);
									});
						});
				::stdexec::sync_wait(::std::move(task));
			}
			else if (sm.is(::boost::sml::state<setting_scene>))
			{

			}
			else if (sm.is(::boost::sml::state<fighting_scene>))
			{

			}
		}

		// Scene resources can be released after the frame tasks and GPU fence have completed.
		if (entitites.controls._current.quit_requested)
		{
			sm.process_event(exit{});
			break;
		}
		if (sm.is(::boost::sml::state<main_menu>))
		{
			auto const activated = entitites.scene.menu.current_state().activated;
			if (activated == 0)
			{
				sm.process_event(start_fighting{});
			}
			else if (activated == 1)
			{
				sm.process_event(open_settings{});
			}
			else if (activated == 2)
			{
				sm.process_event(exit{});
				break;
			}
		}
		else if (sm.is(::boost::sml::state<setting_scene>))
		{
			entitites.draw.set_fullscreen(entitites.settings.preferences().fullscreen);
			if (entitites.settings.current_state().back_requested)
			{
				::save_settings(setting_path, entitites.settings.preferences());;
				sm.process_event(back_to_menu{});
			}
		}
		else if (sm.is(::boost::sml::state<fighting_scene>))
		{
			if (entitites.scene.fighting.current_state().back_requested)
				sm.process_event(back_to_menu{});
		}
		++frame_index;
	}
	::std::println("Rendered {} frames.", frame_index);
}
catch (::std::exception const& error)
{
	::std::println(stderr, "{}", error.what());
	return 1;
}
