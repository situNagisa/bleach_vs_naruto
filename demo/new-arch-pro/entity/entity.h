#pragma once

#include <cassert>
#include <functional>
#include <mutex>
#include <utility>

#include <stdexec/execution.hpp>
#include <vkkl/command_buffer.h>
#include <vkkl/command_pool.h>

#include "./render.h"
#include "../framework/frame_context.h"
#include "../framework/entities.h"
#include "../framework/frame_slot.h"
#include "../framework/any_sender.h"
#include "./detail/demo_vulkan.h"
#include "./detail/immovable.h"

struct entity
{
	struct state : immovable
	{
		explicit state(any_scheduler_type scheduler, ::consumer_arch_vulkan::global_vulkan_env_renderer global)
			: scheduler(::std::move(scheduler))
			, _secondary_command_pool(::consumer_arch_vulkan::create_secondary_command_pool(global))
		{}

		any_scheduler_type scheduler;
		// The buffer must be destroyed before the pool that allocated it.
		::vkkl::command_pool _secondary_command_pool;
		::vkkl::command_buffer _secondary_command_buffer{};
	};

	auto build_task(frame_context& context, entity_view<frame_context> view, task_builder builder) -> void
	{
		if (auto renderer_entity = view.entity<renderer>())
		{
			auto&& re = renderer_entity->get();
			auto render_state = renderer_entity->task<renderer::state>();
			auto render = renderer_entity->task<renderer::task>();
			assert(render_state != nullptr && render != nullptr);

			auto const global = re.vulkan.global_env();
			auto&& state = builder.emplace<entity::state>(context.scheduler, global);
			render_state->recorders.emplace_back(render->begin()
				| ::stdexec::let_value([&state, render_state, global](::std::reference_wrapper<frame_slot> frame)
				{
					return ::stdexec::schedule(state.scheduler)
						| ::stdexec::then([&state, render_state, global, frame]
						{
							state._secondary_command_buffer = ::consumer_arch_vulkan::record_triangle(
								global, frame.get(), state._secondary_command_pool);
							auto lock = ::std::scoped_lock{render_state->secondary.mutex};
							render_state->secondary.commands.push_back(state._secondary_command_buffer);
						});
				}));
		}
	}
};
