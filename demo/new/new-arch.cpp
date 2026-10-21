#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <print>
#include <memory>
#include <optional>
#include <variant>
#include <chrono>
#include <latch>
#include <thread>
#include <tuple>

#include <boost/sml.hpp>

#include <SDL3/SDL_init.h>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/variant_sender.hpp>

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
#include "./entity/fighting_scene/module.h"
#include "./framework/any_sender.h"
#include "./entity/frame_clock.h"
#include "./entity/setting_scene/settings.h"

int main() try
{
	auto sdl = ::bvn::platform::sdl_context{};
	auto pool = ::exec::static_thread_pool{ 4 };
	auto render_scheduler = ::stdexec::run_loop{};
	auto stop_source = ::stdexec::inplace_stop_source{};

	struct exit_scene {};

	module_cache modules{};

	struct entitites_t
	{
		renderer draw{};
		input controls{ draw.window };
		union scene_t
		{
			exit_scene exit;
			main_menu menu;
			fighting_scene fighting;
			setting_scene settings;
			~scene_t() noexcept {}
		} scene{};
		frame_clock clock{};
	} entitites{};

	game_settings preferences{ ::load_settings(setting_path) };
	entitites.draw.set_fullscreen(preferences.fullscreen);

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

				, state<setting_scene> +on_entry<_> / [](entitites_t& entitites) { ::std::construct_at(&entitites.scene.settings, entitites.draw); }
				, state<setting_scene> +on_exit<_> / [](entitites_t& entitites) noexcept { ::std::destroy_at(&entitites.scene.settings); }
				, state<setting_scene> + event<back_to_menu> = state<main_menu>
				, state<setting_scene> + event<exit> = state<exit_scene>

				, state<fighting_scene> + on_entry<_> / [] (entitites_t& entitites) { ::std::construct_at(&entitites.scene.fighting, entitites.controls, entitites.draw, entitites.modules, entitites.workers); }
				, state<fighting_scene> + on_exit<_> / [] (entitites_t& entitites) noexcept { ::std::destroy_at(&entitites.scene.fighting); }
				, state<fighting_scene> + event<back_to_menu> = state<main_menu>
				, state<fighting_scene> + event<exit> = state<exit_scene>
				);
		}
	};

	::boost::sml::sm<machine> sm{ entitites };
	::stdexec::counting_scope async_scope{};
	while (true)
	{
		::std::latch latch{ 1 };
		bool exit_request;

		auto transparent = [](auto depend_sender_productor)
			{
				return ::stdexec::let_value([depend_sender_productor](auto&&... result)
					{
						return ::stdexec::let_value(depend_sender_productor(result...), [&](auto&&... after_result)
							{
								return ::stdexec::just(::std::move(result)..., ::std::move(after_result)...);
							});
					});
			};
		if (sm.is(::boost::sml::state<main_menu>))
		{
			auto task = ::stdexec::starts_on(pool.get_scheduler(), ::stdexec::when_all(
				entitites.draw.begin()
				, ::stdexec::when_all(entitites.clock.tick(), entitites.controls.poll()) | transparent(::std::bind_front(&main_menu::update, &entitites.scene.menu))
				| ::stdexec::let_value([&](renderer::lease& slot, main_menu::state const& snapshot, frame_clock::frame_time const& time, input::state const& current, input::delta const& changes)
					{
						return entitites.scene.menu.record(*slot.token(), snapshot)
							| ::stdexec::let_value([&](::std::vector<::vkkl::command_buffer>& buffers, ::std::vector<buffer_type>& handles)
								{
									return entitites.draw.end(*slot.token(), buffers | ::std::views::transform(&::vkkl::command_buffer::handle) | ::std::ranges::to<::std::vector>())
										| ::stdexec::then([&]
											{
												if (changes.quit_requested)
												{
													sm.process_event(exit{});
													exit_request = true;
													latch.count_down();
													return;
												}
												if (!snapshot.activated)
												{
													exit_request = false;
													latch.count_down();
													return;
												}
												if (snapshot.selected == main_menu::selected_type::start_game)
												{
													sm.process_event(start_fighting{});
													exit_request = false;
													latch.count_down();
													return;
												}
												if (snapshot.selected == main_menu::selected_type::settings)
												{
													sm.process_event(open_settings{});
													exit_request = false;
													latch.count_down();
													return;
												}
												if (snapshot.selected == main_menu::selected_type::exit)
												{
													sm.process_event(exit{});
													exit_request = true;
													latch.count_down();
													return;
												}
												exit_request = false;
												latch.count_down();
												return;
											});
								});
					}));
			::stdexec::spawn(::std::move(task), async_scope.get_token());
		}
		else if (sm.is(::boost::sml::state<setting_scene>))
		{
			auto task = ::stdexec::starts_on(pool.get_scheduler(), ::stdexec::when_all(
				entitites.draw.begin()
				, ::stdexec::when_all(entitites.clock.tick(), entitites.controls.poll()) | ::stdexec::let_value([&](frame_clock::frame_time& time, input::state& current, input::delta& changes)
					{
						auto update = entitites.scene.settings.update(time, current, changes);
						return ::stdexec::when_all(::std::move(update), ::stdexec::just(time, current, changes));
					}))
				| ::stdexec::let_value([&](renderer::lease& slot, setting_scene::state const& snapshot, frame_clock::frame_time const& time, input::state const& current, input::delta const& changes)
					{
						return entitites.scene.settings.record(*slot.token(), snapshot)
							| ::stdexec::let_value([&](::std::vector<::vkkl::command_buffer>& buffers, ::std::vector<buffer_type>& handles)
								{
									return entitites.draw.end(*slot.token(), buffers | ::std::views::transform(&::vkkl::command_buffer::handle) | ::std::ranges::to<::std::vector>())
										| ::stdexec::then([&]
											{
												if (changes.quit_requested)
												{
													::save_settings(setting_path, preferences);
													sm.process_event(exit{});
													exit_request = true;
													latch.count_down();
													return false;
												}
												auto const previous = preferences;
												auto const adjust = static_cast<int>(changes.keyboard.key(input::key_code::right).pressed)
													- static_cast<int>(changes.keyboard.key(input::key_code::left).pressed);
												if (snapshot.selected == setting_scene::selected_type::volume && adjust != 0)
													preferences.volume = ::std::clamp(preferences.volume + adjust * 5, 0, 100);
												else if (snapshot.selected == setting_scene::selected_type::fullscreen && (adjust != 0 || snapshot.activated))
												{
													preferences.fullscreen = !preferences.fullscreen;
													exit_request = false;
													latch.count_down();
													return true;
												}
												auto const back_requested = snapshot.activated && snapshot.selected == setting_scene::selected_type::back;
												if (preferences != previous || back_requested)
													::save_settings(setting_path, preferences);
												if (back_requested)
													sm.process_event(back_to_menu{});
												exit_request = false;
												latch.count_down();
												return false;
											})
										| ::stdexec::then([&](bool fullscreen_changed)
											{
												if (fullscreen_changed)
													entitites.draw.set_fullscreen(preferences.fullscreen);
											});
								});
					}));
			::stdexec::spawn(::std::move(task), async_scope.get_token());
		}
		else if (sm.is(::boost::sml::state<fighting_scene>))
		{
			auto task = ::stdexec::starts_on(pool.get_scheduler(), ::stdexec::when_all(
				entitites.draw.begin()
				, ::stdexec::when_all(entitites.clock.tick(), entitites.controls.poll()) | ::stdexec::let_value([&](frame_clock::frame_time& time, input::state& current, input::delta& changes)
					{
						auto update = entitites.scene.fighting.update(time, current, changes);
						return ::stdexec::when_all(::std::move(update), ::stdexec::just(time, current, changes, fighting_scene::state{}));
					}))
				| ::stdexec::let_value([&](renderer::lease& slot, bool const& back_requested, frame_clock::frame_time const& time, input::state const& current, input::delta const& changes, fighting_scene::state const& snapshot)
					{
						return entitites.scene.fighting.record(*slot.token(), snapshot)
							| ::stdexec::let_value([&](::std::vector<::vkkl::command_buffer>& buffers, ::std::vector<buffer_type>& handles)
								{
									return entitites.draw.end(*slot.token(), buffers | ::std::views::transform(&::vkkl::command_buffer::handle) | ::std::ranges::to<::std::vector>())
										| ::stdexec::then([&]
											{
												if (changes.quit_requested)
												{
													sm.process_event(exit{});
													exit_request = true;
													latch.count_down();
													return;
												}
												if (back_requested)
													sm.process_event(back_to_menu{});
												exit_request = false;
												latch.count_down();
												return;
											});
								});
					}));
			::stdexec::spawn(::std::move(task), async_scope.get_token());
		}
		
		latch.wait();
		if (exit_request)
			break;
	}
	::std::println("Rendered {} frames.", entitites.clock.index());
}
catch (::std::exception const& error)
{
	::std::println(stderr, "{}", error.what());
	return 1;
}
