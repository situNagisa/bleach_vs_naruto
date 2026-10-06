#include <cstdint>
#include <cstdio>
#include <exception>
#include <print>

#include <boost/sml.hpp>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <nagisa/concurrency/when_all_range.h>

#include <bvn/platform/sdl_context.h>

#include "./entity/render.h"
#include "./entity/input.h"
#include "./entity/main_menu.h"
#include "./framework/frame_context.h"



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

			~scene_t() noexcept {}
		} scene{};
	} entitites{};

	auto frame_index = ::std::uint64_t{};

	struct exit {};

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
				, state<main_menu> +event<exit> = state<exit_scene>
				);
		}
	};

	::boost::sml::sm<machine> sm{ entitites };

	for (;;)
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

		::std::variant<::std::monostate, main_menu::task> st{};

		if (sm.is(::boost::sml::state<main_menu>))
		{
			auto&& mmt = st.emplace<main_menu::task>(entitites.scene.menu, it, entitites.draw, rt);
			entitites.scene.menu.build_task(mmt, entitites.draw, rt);
		}

		::stdexec::sync_wait(::nagisa::concurrency::when_all_range(context.roots));

		if (sm.is(::boost::sml::state<main_menu>))
		{
			if (entitites.scene.menu.current_state().activated.has_value() && entitites.scene.menu.current_state().activated.value() == 2)
			{
				sm.process_event(exit{});
				break;
			}
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
