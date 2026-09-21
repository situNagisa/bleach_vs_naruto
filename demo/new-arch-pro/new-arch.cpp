#include <cstdint>
#include <cstdio>
#include <exception>
#include <print>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <nagisa/concurrency/when_all_range.h>

#include <bvn/platform/sdl_context.h>

#include "./entity/render.h"
#include "./entity/input.h"
#include "./entity/main_menu.h"
#include "./framework/entities.h"
#include "./framework/frame_context.h"


int main() try
{
	auto sdl = ::bvn::platform::sdl_context{};
	auto pool = ::exec::static_thread_pool{ 4 };
	auto stop_source = ::stdexec::inplace_stop_source{};

	auto draw = renderer{  };
	auto controls = input{draw.window};
	auto a = main_menu{};

	auto frame_index = ::std::uint64_t{};
	for (;;)
	{
		frame_context context{
			.index = frame_index,
			.scheduler = pool.get_scheduler(),
			.stop_token = stop_source.get_token(),
		};

		auto world = entity_storage<frame_context>{};
		world.add(controls);
		world.add(draw);
		world.add(a);
		
		::build_all(world, context);
		::stdexec::sync_wait(::nagisa::concurrency::when_all_range(context.roots));
		++frame_index;
	}
	::std::println("Rendered {} frames.", frame_index);
}
catch (::std::exception const& error)
{
	::std::println(stderr, "{}", error.what());
	return 1;
}
