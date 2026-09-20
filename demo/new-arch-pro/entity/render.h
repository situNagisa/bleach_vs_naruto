#pragma once

#include <functional>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include <nagisa/concurrency/lease.h>
#include <nagisa/concurrency/when_all_range.h>

#include "../framework/frame_context.h"
#include "../framework/entities.h"
#include "../framework/frame_slot.h"
#include "../framework/any_sender.h"

#include "./detail/demo_vulkan.h"
#include "./detail/immovable.h"

struct renderer
{
	template<class T>
	using resource_pool = ::nagisa::concurrency::bounded_lease_pool<T*, 4>;

	template <class Begin, class End, class Fence>
	struct _senders
	{
		Begin _begin;
		End _end;
		Fence _fence;

		[[nodiscard]] auto&& begin() const noexcept { return _begin; }
		[[nodiscard]] auto&& end() const noexcept { return _end; }
		[[nodiscard]] auto&& fence() const noexcept { return _fence; }
	};

	template <class B, class E, class F>
	_senders(B, E, F) -> _senders<B, E, F>;

	struct state : immovable
	{
		::std::vector<any_sender_type> recorders{};
		struct
		{
			::std::mutex mutex{};
				// Entity states own the buffers and keep them alive through fence completion.
				::std::vector<::vkkl::command_buffer_observer> commands{};
		} secondary{};
		any_scheduler_type scheduler{};
	};

	struct _task_fn
	{
		constexpr auto operator()(state& state, resource_pool<frame_slot>& slot,
		                          consumer_arch_vulkan::vulkan_context& vulkan) const
		{
			auto begin = slot.acquire()
				| ::stdexec::then([&vulkan](resource_pool<frame_slot>::lease slot) -> resource_pool<frame_slot>::lease
				{
					_begin(vulkan.global_env(), *slot.token());
					return slot;
				})
				| ::exec::split()
				| ::stdexec::then([](resource_pool<frame_slot>::lease const& frame)
				{
					return ::std::ref(*frame.token());
				});

			auto end = begin
				| ::stdexec::let_value([&](::std::reference_wrapper<frame_slot> frame)
				{
					return ::nagisa::concurrency::when_all_range(state.recorders | ::std::views::as_rvalue)
						| ::stdexec::continues_on(state.scheduler)
						| ::stdexec::then([&, frame]
						{
							auto handles = state.secondary.commands
								| ::std::views::transform([](::vkkl::command_buffer_observer command)
									{
										return command.handle;
									})
								| ::std::ranges::to<::std::vector>();
							_submit_present_frame(vulkan.global_env(), frame.get(), handles);
							return frame;
						});
				})
				| ::exec::split();
			auto fence = end
				| ::stdexec::then([&](::std::reference_wrapper<frame_slot> frame)
				{
					_wait_fence(vulkan.global_env(), frame.get());
				})
				| ::exec::split();

			return _senders{
				::std::move(begin),
				::std::move(end),
				::std::move(fence),
			};
		}
	};

	using task = task_data<_task_fn, state&, resource_pool<frame_slot>&, consumer_arch_vulkan::vulkan_context&>;

	void build_task(frame_context& context, entity_view<frame_context>, task_builder builder)
	{
		auto&& state = builder.emplace<renderer::state>();
		state.scheduler = context.scheduler;
		auto&& t = builder.emplace<task>(state, _pool, vulkan);
		context.roots.emplace_back(t.fence());
	}

	static frame_slot _factory(const ::consumer_arch_vulkan::global_vulkan_env_renderer& context)
	{
		auto device = ::vkkl::device_observer{context.device()};
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
			::std::span{&raw_command_buffer, 1u}
		);
		auto slot = frame_slot{
			._in_flight = ::vkkl::fence{device.handle, ::vkfu::create_fence(device.handle, ::vkfu::param::fence{})},
			._primary_command_pool = ::vkkl::command_pool{device.handle, command_pool},
			._primary_command_buffer = ::vkkl::command_buffer{device.handle, command_pool, raw_command_buffer},
			._image_available = ::vkkl::semaphore{
				device.handle, ::vkfu::create_semaphore(device.handle, ::vkfu::param::semaphore{})
			},
			._render_finished = ::vkkl::semaphore{
				device.handle, ::vkfu::create_semaphore(device.handle, ::vkfu::param::semaphore{})
			},
			._active_image_index = {},
			._active_image = {},
			._active_image_view = {},
			._depth_image = {},
			._depth_image_view = {},
			._extent = {},
		};
		return slot;
	}

	static void _begin(::consumer_arch_vulkan::global_vulkan_env_renderer const& global, frame_slot& frame)
	{
		frame._active_image_index = ::vkfu::khr::acquire_next_image2(global.device(),
			::vkfu::param::khr::acquire_next_image{
				.swapchain = global.swapchain(),
				.timeout = (::std::numeric_limits<::std::uint64_t>::max)(),
				.semaphore = frame.image_available(),
				.fence = VK_NULL_HANDLE,
				.device_mask = (::std::numeric_limits<::std::uint32_t>::max)(),
			});
		frame._active_image = global.swapchain_images()[frame._active_image_index];
		frame._active_image_view = global.swapchain_image_views()[frame._active_image_index];
		frame._extent = global.swapchain_extent();

		consumer_arch_vulkan::check(::vkResetCommandPool(global.device(), frame.primary_command_pool(), 0),
		                            "failed to reset primary command pool");
		auto in_flight = frame.in_flight();
		::vkfu::reset_fences(global.device(), ::std::span{&in_flight, 1u});
		namespace param = ::vkfu::param;
		using namespace ::vkfu::enums;
		::vkfu::begin_command_buffer(frame.primary_command_buffer(),
		                             param::command_buffer_begin{.flags = {.one_time_submit = 1},});
		const auto acquire_barrier = ::vkfu::evaluate(param::image_memory_barrier2{
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
		::vkfu::cmd_pipeline_barrier2(frame.primary_command_buffer(),
		                              param::dependency{.image_memory_barriers = ::std::span{&acquire_barrier, 1u},});
		auto clear_value = ::VkClearValue{};
		clear_value.color = {{0.025f, 0.035f, 0.055f, 1.0f}};
		const auto color_attachment = ::vkfu::evaluate(param::rendering_attachment{
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
		const ::consumer_arch_vulkan::global_vulkan_env_renderer& global
		, const frame_slot& frame
		, ::std::span<VkCommandBuffer> secondary_commands
	)
	{
		const auto primary_command_buffer = frame.primary_command_buffer();
		if (!secondary_commands.empty())
			::vkfu::cmd_execute_commands(primary_command_buffer, secondary_commands);
		::vkCmdEndRendering(primary_command_buffer);
		namespace param = ::vkfu::param;
		using namespace ::vkfu::enums;

		const auto present_barrier = ::vkfu::evaluate(param::image_memory_barrier2{
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
		consumer_arch_vulkan::check(::vkEndCommandBuffer(primary_command_buffer),
		                            "failed to end primary command buffer");

		const auto image_available = frame.image_available();
		const auto render_finished = frame.render_finished();
		const auto wait_semaphore = ::vkfu::evaluate(param::semaphore_submit{
			.semaphore = image_available, .stage_mask = {.color_attachment_output = 1},
		});
		const auto command_buffer = ::vkfu::evaluate(param::command_buffer_submit{
			.command_buffer = primary_command_buffer,
		});
		const auto signal_semaphore = ::vkfu::evaluate(param::semaphore_submit{
			.semaphore = render_finished, .stage_mask = {.all_commands = 1},
		});
		const auto submit = ::vkfu::evaluate(param::submit2{
			.wait_semaphore_infos = ::std::span{&wait_semaphore, 1u},
			.command_buffer_infos = ::std::span{&command_buffer, 1u},
			.signal_semaphore_infos = ::std::span{&signal_semaphore, 1u},
		});
		::vkfu::queue_submit2(global.graphics_queue(), ::std::span{&submit, 1u}, frame.in_flight());

		const auto swapchain = global.swapchain();
		const auto active_image_index = frame.active_image_index();
		const auto outcome = ::vkfu::khr::queue_present(
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
		// Submission already succeeded; finish GPU work before frame resources can unwind.
		_wait_fence(global, frame);
		throw ::std::runtime_error{"failed to present Vulkan frame"};
	}

	static void _wait_fence(const ::consumer_arch_vulkan::global_vulkan_env_renderer& global, const frame_slot& frame)
	{
		const auto in_flight = frame.in_flight();
		::vkfu::wait_for_fences(global.device(), ::std::span{&in_flight, 1u}, VK_TRUE,
		                        (::std::numeric_limits<::std::uint64_t>::max)());
		consumer_arch_vulkan::check(::vkQueueWaitIdle(global.present_queue()), "failed to wait for the present queue");
	}

	::bvn::platform::window window{"vkkl Vulkan secondary triangle", 960, 540};
	consumer_arch_vulkan::vulkan_context vulkan{window};
	::std::array<frame_slot, 4> _frames{ 
		_factory(vulkan.global_env()),
		_factory(vulkan.global_env()),
		_factory(vulkan.global_env()),
		_factory(vulkan.global_env()),
	};
	resource_pool<frame_slot> _pool{::std::in_place, ::std::from_range, _frames | ::std::views::transform([](auto& frame) { return &frame; })};
};
