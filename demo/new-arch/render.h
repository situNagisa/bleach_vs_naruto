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
						_begin(vulkan.global_env(), slot.get());
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
									auto handles = state.secondary.commands
										| ::std::views::transform([](::vkkl::command_buffer_observer command) { return command.handle; })
										| ::std::ranges::to<::std::vector>();
									_submit_present_frame(vulkan.global_env(), frame.get(), handles);
									return frame;
								});
					})
				| ::exec::split();
			auto fence = end
				| ::stdexec::then([&](resource_pool<frame_slot>::lease const& frame)
					{
						_wait_fence(vulkan.global_env(), frame.get());
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
		t.scheduler = context.scheduler;
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

	static void _begin(::consumer_arch_vulkan::global_vulkan_env_renderer const& global, frame_slot const& frame)
	{
		consumer_arch_vulkan::check(::vkResetCommandPool(global.device(), frame.primary_command_pool(), 0), "failed to reset primary command pool");
		auto in_flight = frame.in_flight();
		::vkfu::reset_fences(global.device(), ::std::span{ &in_flight, 1u });
		namespace param = ::vkfu::param;
		using namespace ::vkfu::enums;
		::vkfu::begin_command_buffer(frame.primary_command_buffer(), param::command_buffer_begin{ .flags = {.one_time_submit = 1}, });
		auto const acquire_barrier = ::vkfu::evaluate(param::image_memory_barrier2{
			.dst_stage_mask = {.color_attachment_output = 1},
			.dst_access_mask = {.color_attachment_write = 1},
			.old_layout = image_layout::undefined,
			.new_layout = image_layout::color_attachment_optimal,
			.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
			.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
			.image = frame.active_image(),
			.subresource_range = {
				.aspectMask = ::VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		});
		::vkfu::cmd_pipeline_barrier2(frame.primary_command_buffer(), param::dependency{ .image_memory_barriers = ::std::span{&acquire_barrier, 1u}, });
		auto clear_value = ::VkClearValue{};
		clear_value.color = { {0.025f, 0.035f, 0.055f, 1.0f} };
		auto const color_attachment = ::vkfu::evaluate(param::rendering_attachment{
			.image_view = frame.active_image_view(),
			.image_layout = image_layout::color_attachment_optimal,
			.load_op = attachment_load_op::clear,
			.store_op = attachment_store_op::store,
			.clear_value = clear_value,
			});
		::vkfu::cmd_begin_rendering(frame.primary_command_buffer(), param::rendering<>{
			.flags = {.contents_secondary_command_buffers = 1},
			.render_area = ::VkRect2D{.offset = {}, .extent = frame.extent()},
			.layer_count = 1,
			.color_attachments = ::std::span{&color_attachment, 1u},
			});
	}
	static void _submit_present_frame(
		::consumer_arch_vulkan::global_vulkan_env_renderer const& global
		, frame_slot const& frame
		, ::std::span<VkCommandBuffer> secondary_commands
		)
	{
		auto const primary_command_buffer = frame.primary_command_buffer();
		if (!secondary_commands.empty())
			::vkfu::cmd_execute_commands(primary_command_buffer, secondary_commands);
		::vkCmdEndRendering(primary_command_buffer);
		namespace param = ::vkfu::param;
		using namespace ::vkfu::enums;

		auto const present_barrier = ::vkfu::evaluate(param::image_memory_barrier2{
			.src_stage_mask = {.color_attachment_output = 1},
			.src_access_mask = {.color_attachment_write = 1},
			.old_layout = image_layout::color_attachment_optimal,
			.new_layout = image_layout::present_src,
			.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
			.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
			.image = frame.active_image(),
			.subresource_range = {
				.aspectMask = ::VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			});
		::vkfu::cmd_pipeline_barrier2(primary_command_buffer, param::dependency{
			.image_memory_barriers = ::std::span{&present_barrier, 1u},
			});
		consumer_arch_vulkan::check(::vkEndCommandBuffer(primary_command_buffer), "failed to end primary command buffer");

		auto const image_available = frame.image_available();
		auto const render_finished = frame.render_finished();
		auto const wait_semaphore = ::vkfu::evaluate(param::semaphore_submit{ .semaphore = image_available,.stage_mask = {.color_attachment_output = 1}, });
		auto const command_buffer = ::vkfu::evaluate(param::command_buffer_submit{ .command_buffer = primary_command_buffer, });
		auto const signal_semaphore = ::vkfu::evaluate(param::semaphore_submit{ .semaphore = render_finished,.stage_mask = {.all_commands = 1}, });
		auto const submit = ::vkfu::evaluate(param::submit2{
			.wait_semaphore_infos = ::std::span{&wait_semaphore, 1u},
			.command_buffer_infos = ::std::span{&command_buffer, 1u},
			.signal_semaphore_infos = ::std::span{&signal_semaphore, 1u},
			});
		::vkfu::queue_submit2(global.graphics_queue(), ::std::span{ &submit, 1u }, frame.in_flight());

		auto const swapchain = global.swapchain();
		auto const active_image_index = frame.active_image_index();
		auto const outcome = ::vkfu::khr::queue_present(
			global.present_queue(),
			param::khr::present{
				.wait_semaphores = ::std::span{&render_finished, 1u},
				.swapchain_count = 1,
				.swapchains = &swapchain,
				.image_indices = &active_image_index,
			},
			::std::nothrow
			);
		if (outcome || outcome.error() == ::VK_SUBOPTIMAL_KHR)
			return;
		throw ::std::runtime_error{ "failed to present Vulkan frame" };
	}
	static void _wait_fence(::consumer_arch_vulkan::global_vulkan_env_renderer const& global, frame_slot const& frame)
	{
		auto const in_flight = frame.in_flight();
		::vkfu::wait_for_fences(global.device(), ::std::span{ &in_flight, 1u }, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());
		consumer_arch_vulkan::check(::vkQueueWaitIdle(global.present_queue()), "failed to wait for the present queue");
	}

	::bvn::platform::window window{ "vkkl Vulkan secondary triangle", 960, 540 };
	consumer_arch_vulkan::vulkan_context vulkan{ window };
	resource_pool<frame_slot> _pool{ 3, [this](::std::size_t i) { return _factory(vulkan.global_env()); } };
};