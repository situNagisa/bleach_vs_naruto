#include <cassert>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <nagisa/concurrency/when_all_range.h>

#include "./render.h"

namespace
{
	auto factory(::consumer_arch_vulkan::global_vulkan_env_renderer const& context) -> frame_slot
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

	void begin(::consumer_arch_vulkan::global_vulkan_env_renderer const& global, frame_slot& frame)
	{
		frame._active_image_index = ::vkfu::khr::acquire_next_image2(global.device(),
			::vkfu::param::khr::acquire_next_image{
				.swapchain = global.swapchain(),
				.timeout = (::std::numeric_limits<::std::uint64_t>::max)(),
				.semaphore = frame.image_available(),
				.fence = VK_NULL_HANDLE,
				.device_mask = 1
			});
		frame._active_image = global.swapchain_images()[frame._active_image_index];
		frame._active_image_view = global.swapchain_image_views()[frame._active_image_index];
		frame._extent = global.swapchain_extent();

		::consumer_arch_vulkan::check(::vkResetCommandPool(global.device(), frame.primary_command_pool(), 0),
			"failed to reset primary command pool");
		auto in_flight = frame.in_flight();
		::vkfu::reset_fences(global.device(), ::std::span{ &in_flight, 1u });
		namespace param = ::vkfu::param;
		using namespace ::vkfu::enums;
		::vkfu::begin_command_buffer(frame.primary_command_buffer(),
			param::command_buffer_begin{ .flags = {.one_time_submit = 1}, });
		auto const acquire_barrier = ::vkfu::evaluate(param::image_memory_barrier2{
			.dst_stage_mask = {.color_attachment_output = 1},
			.dst_access_mask = {.color_attachment_write = 1},
			.old_layout = image_layout::undefined,
			.new_layout = image_layout::color_attachment_optimal,
			.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
			.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
			.image = frame.active_image(),
			.subresource_range = {
				.aspect_mask = {.color = 1},
				.base_mip_level = 0,
				.level_count = 1,
				.base_array_layer = 0,
				.layer_count = 1,
			},
			});
		::vkfu::cmd_pipeline_barrier2(frame.primary_command_buffer(),
			param::dependency{ .image_memory_barriers = ::std::span{&acquire_barrier, 1u}, });
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
			.flags = { .contents_secondary_command_buffers = 1 },
				.render_area = ::VkRect2D{ .offset = {}, .extent = frame.extent() },
				.layer_count = 1,
				.color_attachments = ::std::span{ &color_attachment, 1u },
		});
	}


	void wait_fence(::consumer_arch_vulkan::global_vulkan_env_renderer const& global, frame_slot const& frame)
	{
		auto const in_flight = frame.in_flight();
		::vkfu::wait_for_fences(global.device(), ::std::array{ in_flight }, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());
		::consumer_arch_vulkan::check(::vkQueueWaitIdle(global.present_queue()), "failed to wait for the present queue");
	}

	void submit_present_frame(
		::consumer_arch_vulkan::global_vulkan_env_renderer const& global
		, frame_slot const& frame
		, ::std::span<VkCommandBuffer const> secondary_commands
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
				.aspect_mask = {.color = 1},
				.base_mip_level = 0,
				.level_count = 1,
				.base_array_layer = 0,
				.layer_count = 1,
			},
			});
		::vkfu::cmd_pipeline_barrier2(primary_command_buffer, param::dependency{ .image_memory_barriers = ::std::span{&present_barrier, 1u}, });
		::consumer_arch_vulkan::check(::vkEndCommandBuffer(primary_command_buffer), "failed to end primary command buffer");

		::vkfu::queue_submit2(global.graphics_queue(), ::std::array{ ::vkfu::evaluate(param::submit2{
			.wait_semaphore_infos = ::std::array{ ::vkfu::evaluate(param::semaphore_submit{ .semaphore = frame.image_available(), .stage_mask = {.color_attachment_output = 1}, })},
			.command_buffer_infos = ::std::array{ ::vkfu::evaluate(param::command_buffer_submit{ .command_buffer = primary_command_buffer, }) },
			.signal_semaphore_infos = ::std::array{ ::vkfu::evaluate(param::semaphore_submit{ .semaphore = frame.render_finished(), .stage_mask = {.all_commands = 1}, }) },
			}) }, frame.in_flight());

		auto const swapchain = global.swapchain();
		auto const active_image_index = frame.active_image_index();
		auto const outcome = ::vkfu::khr::queue_present(
			global.present_queue(),
			param::khr::present{
				.wait_semaphores = ::std::array{ frame.render_finished() },
				.swapchain_count = 1,
				.swapchains = &swapchain,
				.image_indices = &active_image_index,
			},
			::std::nothrow
			);
		if (outcome || outcome.error() == ::VK_SUBOPTIMAL_KHR)
			return;
		// Submission already succeeded; finish GPU work before frame resources can unwind.
		::wait_fence(global, frame);
		throw ::std::runtime_error{ "failed to present Vulkan frame" };
	}
}

renderer::renderer()
	: window("vkkl Vulkan secondary triangle", 960, 540)
	, vulkan(window)
	, _frames{
		::factory(vulkan.global_env()),
		::factory(vulkan.global_env()),
		::factory(vulkan.global_env()),
		::factory(vulkan.global_env())
	}
	, _pool(::std::in_place, ::std::from_range, _frames | ::std::views::transform([](auto& frame) { return &frame; }))
{}

values_sender_type<renderer::lease> renderer::begin()
{
	return _pool.acquire()
		| ::stdexec::then([this](resource_pool::lease slot) -> resource_pool::lease
			{
				::begin(vulkan.global_env(), *slot.token());
				return slot;
			});
}

values_sender_type<> renderer::end(frame_slot& frame, std::span<VkCommandBuffer const> commands)
{
	return ::stdexec::just()
		| ::stdexec::then([this, &frame, commands]
			{
				::submit_present_frame(vulkan.global_env(), frame, commands);
				::wait_fence(vulkan.global_env(), frame);
			});
}

void renderer::set_fullscreen(bool enabled)
{
	if (((::SDL_GetWindowFlags(window.handle) & SDL_WINDOW_FULLSCREEN) != 0) == enabled)
		return;
	::consumer_arch_vulkan::check(::vkDeviceWaitIdle(vulkan.global_env().device()), "fullscreen: wait for device");
	if (!::SDL_SetWindowFullscreen(window.handle, enabled) || !::SDL_SyncWindow(window.handle))
		throw ::std::runtime_error{ ::std::string{ "fullscreen: " } + ::SDL_GetError() };
	::consumer_arch_vulkan::create_swapchain(vulkan, window);
}
