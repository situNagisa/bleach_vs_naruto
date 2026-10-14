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

			auto rop = entitites.draw.connect(context.scheduler);
			auto rt = rop.start(context);
			auto iop = entitites.controls.connect();
			auto it = iop.start();

			union 
			{
				::nagisa::concurrency::details::manual_lifetime<main_menu::operation_state> menu;
				::nagisa::concurrency::details::manual_lifetime<setting_scene::operation_state> setting;
				::nagisa::concurrency::details::manual_lifetime<fighting_scene::operation_state> fighting;
			}scene_op{};

			if (sm.is(::boost::sml::state<main_menu>))
			{
				auto&& mmop = scene_op.menu.construct_from(&main_menu::connect, &entitites.scene.menu, entitites.controls, it, entitites.draw, rt);
				auto mmt = mmop.start(context);
			}
			else if (sm.is(::boost::sml::state<setting_scene>))
			{
				auto&& setting_op = scene_op.setting.construct_from(&setting_scene::connect, &entitites.settings, entitites.controls, it, entitites.draw, rt);
				auto setting_shared = setting_op.start(context);
			}
			else if (sm.is(::boost::sml::state<fighting_scene>))
			{
				auto&& fighting_op = scene_op.fighting.construct_from(&fighting_scene::connect, &entitites.scene.fighting, entitites.controls, it, entitites.draw, rt);
				auto fighting_shared = fighting_op.start(context);
			}

			::stdexec::sync_wait(::nagisa::concurrency::when_all_range(context.roots));

			if (sm.is(::boost::sml::state<main_menu>))
			{
				scene_op.menu.destroy();
			}
			else if (sm.is(::boost::sml::state<setting_scene>))
			{
				scene_op.setting.destroy();
			}
			else if (sm.is(::boost::sml::state<fighting_scene>))
			{
				scene_op.fighting.destroy();
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
