
#include <ranges>
#include <algorithm>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/any_sender_of.hpp>

#include "./render.h"
#include "./entity.h"
#include "./entities.h"
#include "./dynamic_when_all.h"
#include "./frame_context.h"


int main()
{
	auto pool = ::exec::static_thread_pool{ 4 };
	auto stop_source = ::stdexec::inplace_stop_source{};

	auto draw = renderer{  };
	auto a = entity{ };

	for (auto frame_index : ::std::views::iota(0u, 4u))
	{
		frame_context context{
			.index = frame_index,
			.scheduler = pool.get_scheduler(),
			.stop_token = stop_source.get_token(),
		};

		auto world = entity_storage<frame_context>{};
		world.add(draw);
		world.add(a);
		build_all(world, context);
		::stdexec::sync_wait(::dynamic_when_all(context.roots));
	}
}