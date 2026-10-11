#include <cstdint>
#include <cstdio>
#include <exception>
#include <print>
#include <memory>
#include <variant>
#include <chrono>
#include <thread>

#include <boost/sml.hpp>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <nagisa/concurrency/when_all_range.h>

#include <bvn/platform/sdl_context.h>

#include "./entity/render.h"
#include "./entity/input.h"
#include "./entity/main_menu.h"
#include "./entity/setting_scene.h"
#include "./framework/frame_context.h"
#include "./settings.h"



int main() try
{
	auto sdl = ::bvn::platform::sdl_context{};
	auto pool = ::exec::static_thread_pool{ 4 };
	auto stop_source = ::stdexec::inplace_stop_source{};

	struct exit_scene {};

	struct entitites_t
	{
		::std::filesystem::path settings_path = ::settings_file_path();
		game_settings preferences = ::load_settings(settings_path);
		renderer draw{};
		input controls{ draw.window };
		union scene_t
		{
			exit_scene exit;
			main_menu menu;
			setting_scene settings;

			~scene_t() noexcept {}
		} scene{};
	} entitites{};
	entitites.draw.set_fullscreen(entitites.preferences.fullscreen);
	auto saved_settings = entitites.preferences;

	auto frame_index = ::std::uint64_t{};

	struct exit {};
	struct open_settings {};
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
				, state<setting_scene> + on_entry<_> / [] (entitites_t& entitites) { ::std::construct_at(&entitites.scene.settings, entitites.draw, entitites.preferences); }
				, state<setting_scene> + on_exit<_> / [] (entitites_t& entitites) noexcept { ::std::destroy_at(&entitites.scene.settings); }
				, state<setting_scene> + event<back_to_menu> = state<main_menu>
				, state<setting_scene> + event<exit> = state<exit_scene>
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

			auto rt = renderer::task(entitites.draw, context.scheduler);
			entitites.draw.build_task(context, rt);
			auto it = input::task(entitites.controls);
			entitites.controls.build_task(it);

			::std::variant<::std::monostate, main_menu::task, setting_scene::task> st{};

			if (sm.is(::boost::sml::state<main_menu>))
			{
				auto&& mmt = st.emplace<main_menu::task>(entitites.scene.menu, it, entitites.draw, rt);
				entitites.scene.menu.build_task(context, mmt, entitites.draw, rt);
			}
			else if (sm.is(::boost::sml::state<setting_scene>))
			{
				auto&& settings_task = st.emplace<setting_scene::task>(entitites.scene.settings, it, entitites.draw, rt);
				entitites.scene.settings.build_task(context, settings_task, entitites.draw, rt);
			}

			::stdexec::sync_wait(::nagisa::concurrency::when_all_range(context.roots));
		}

		if (entitites.preferences != saved_settings)
		{
			entitites.draw.set_fullscreen(entitites.preferences.fullscreen);
			::save_settings(entitites.settings_path, entitites.preferences);
			saved_settings = entitites.preferences;
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
			if (activated == 1)
			{
				sm.process_event(open_settings{});
			}
			else if (activated == 2)
			{
				sm.process_event(exit{});
				break;
			}
		}
		else if (sm.is(::boost::sml::state<setting_scene>) && entitites.scene.settings.current_state().back_requested)
		{
			sm.process_event(back_to_menu{});
		}
		++frame_index;
		if (entitites.preferences.frame_rate != 0)
		{
			auto const interval = ::std::chrono::duration<double>{ 1.0 / entitites.preferences.frame_rate };
			::std::this_thread::sleep_until(frame_started + interval);
		}
	}
	::std::println("Rendered {} frames.", frame_index);
}
catch (::std::exception const& error)
{
	::std::println(stderr, "{}", error.what());
	return 1;
}
