#pragma once

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "./frame_context.h"
#include "./entities.h"
#include "./frame_slot.h"
#include "./demo_vulkan.h"
#include "./any_sender.h"
#include "./dynamic_when_all.h"

struct renderer
{

	template <class Begin, class End, class Fence>
	struct _task
	{
		Begin _begin;
		End _end;
		Fence _fence;

		[[nodiscard]] auto&& begin() const noexcept { return _begin; }
		[[nodiscard]] auto&& end() const noexcept { return _end; }
		[[nodiscard]] auto&& fence() const noexcept { return _fence; }
	};
	template <class B, class E, class F>
	_task(B, E, F) -> _task<B, E, F>;

	struct _task_state
	{
		::std::vector<any_sender_type> recorders;
		struct
		{
			::std::mutex mutex{};
			::std::vector<::vkkl::command_buffer> commands{};
		}secondary{};
		any_scheduler_type scheduler;
	};

	struct _task_fn
	{
		constexpr auto operator()(_task_state& state, resource_pool<frame_slot>& slot, consumer_arch_vulkan::vulkan_context& vulkan) const
		{
			auto begin = slot.acquire()
				| ::stdexec::then([&vulkan](resource_pool<frame_slot>::lease slot)
					 {
					 	auto dynamic_slot = ::bvn::graphics::dynamic_forward_frame_env_renderer(slot.get());
					 	::consumer_arch_vulkan::begin_frame(vulkan.global_env(), dynamic_slot);
					 	return slot;
					 })
				| ::exec::split();

			auto end = begin
				| ::stdexec::let_value([&](resource_pool<frame_slot>::lease const& frame)
					{
						return ::dynamic_when_all(state.recorders | ::std::views::as_rvalue)
							| ::stdexec::continues_on(state.scheduler)
							| ::stdexec::then([&]-> auto&&
								{
									auto const renderer = vulkan.global_env();
									auto queue_lock = ::consumer_arch_vulkan::lock_temporary_queue_synchronization(renderer);
									auto handles = state.secondary.commands
										| ::std::views::transform([](::vkkl::command_buffer_observer command) { return command.handle; })
										| ::std::ranges::to<::std::vector>();
									auto const present_result = ::consumer_arch_vulkan::submit_present_frame(renderer, ::bvn::graphics::dynamic_forward_frame_env_renderer(frame.get()), handles);
									::consumer_arch_vulkan::check_present_result(present_result);
									return frame;
								});
					})
				| ::exec::split();
			auto fence = end
				| ::stdexec::then([&](resource_pool<frame_slot>::lease const& frame)
					{
						auto const renderer = vulkan.global_env();
						::consumer_arch_vulkan::wait_for_frame_gpu(renderer, ::bvn::graphics::dynamic_forward_frame_env_renderer(frame.get()));
					})
				| ::exec::split();

			return _task{
				::std::move(begin),
				::std::move(end),
				::std::move(fence),
			};
		}
	};
	using task = stateful_task_data<_task_fn, _task_state, resource_pool<frame_slot>&, consumer_arch_vulkan::vulkan_context&>;

	auto build_task(frame_context& context, entity_view<frame_context>, task_builder builder) -> void
	{
		auto&& t = builder.emplace<task>(_pool, vulkan);
		context.roots.push_back(t.fence());
	}

	static frame_slot _factory(::consumer_arch_vulkan::global_vulkan_env_renderer const& context)
	{
		auto device = ::vkkl::device_observer{ context.device() };
		auto command_pool = ::vkfu::create_command_pool(device.handle, ::vkfu::param::command_pool{
				.flags = {.transient = 1, .reset_command_buffer = 1},
				.queue_family_index = context.graphics_queue_family(),
			});
		auto raw_command_buffer = ::VkCommandBuffer{};
		::vkfu::allocate_command_buffers(
			device.handle,
			::vkfu::param::command_buffer{
				.command_pool = command_pool,
				.level = ::vkfu::enums::command_buffer_level::primary,
				.command_buffer_count = 1,
			},
			::std::span{ &raw_command_buffer, 1u }
			);
		auto slot = frame_slot{
			._in_flight = ::vkkl::fence{device.handle, ::vkfu::create_fence(device.handle, ::vkfu::param::fence{})},
			._primary_command_pool = ::vkkl::command_pool{device.handle, command_pool},
			._primary_command_buffer = ::vkkl::command_buffer{device.handle, command_pool, raw_command_buffer},
			._image_available = ::vkkl::semaphore{device.handle, ::vkfu::create_semaphore(device.handle, ::vkfu::param::semaphore{})},
			._render_finished = ::vkkl::semaphore{device.handle, ::vkfu::create_semaphore(device.handle, ::vkfu::param::semaphore{})},
			._active_image_index = {},
			._active_image = {},
			._active_image_view = {},
			._depth_image = {},
			._depth_image_view = {},
			._extent = {},
		};
		return slot;
	}

	any_scheduler_type _scheduler;
	::bvn::platform::window window{ "vkkl Vulkan secondary triangle", 960, 540 };
	consumer_arch_vulkan::vulkan_context vulkan{ window };
	resource_pool<frame_slot> _pool{ 3, [this](::std::size_t i) { return _factory(vulkan.global_env()); } };
};