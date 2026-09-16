#pragma once

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "./frame_context.h"
#include "./entities.h"
#include "./frame_slot.h"
#include "./demo_vulkan.h"

struct renderer
{
	struct task
	{
		// static auto _begin_sender(frame_slot_resource& slot, consumer_arch_vulkan::vulkan_context& vulkan) noexcept
		// {
		// 	return ::stdexec::just(slot.acquire())
		// 		| ::stdexec::then([&vulkan](frame_slot_resource::forward_slot_type& slot)
		// 			{
		// 				auto dynamic_slot = ::bvn::graphics::dynamic_forward_frame_env_renderer(::std::move(slot));
		// 				::consumer_arch_vulkan::begin_frame(vulkan.global_env(), dynamic_slot);
		// 				return dynamic_slot;
		// 			})
		// 		| ::exec::split();
		// }
		// decltype(_begin_sender(::std::declval<frame_slot_resource&>(), ::std::declval<consumer_arch_vulkan::vulkan_context&>())) _begin;
		// 
		// constexpr auto begin() const noexcept { return _begin; }
	};

	auto build_task(frame_context& context, entity_view<frame_context>, task_builder builder) -> void
	{
		auto&& t = builder.emplace<task>();
		// context.roots.push_back(t.begin());
	}


	::bvn::platform::window window{ "vkkl Vulkan secondary triangle", 960, 540 };
	consumer_arch_vulkan::vulkan_context vulkan{ window };
	frame_slot_resource _slots{vulkan.global_env(), 3};
};