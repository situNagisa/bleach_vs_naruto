module;

#include <cassert>

#include <vulkan/vulkan.h>

#include <vkfu/vulkan/v1.4.328/version_1_0.h>
#include <vkfu/vulkan/v1.4.328/version_1_1.h>
#include <vkfu/vulkan/v1.4.328/version_1_3.h>
#include <vkfu/vulkan/v1.4.328/khr/swapchain.h>
#include <vkfu/vulkan/v1.4.328/ext/debug_utils.h>
#include <vkfu/vulkan/v1.4.328/layer/names.h>

#include <vkkl/instance.h>
#include <vkkl/device.h>
#include <vkkl/physical_device.h>
#include <vkkl/command_buffer.h>
#include <vkkl/swapchain.h>
#include <vkkl/image_view.h>
#include <vkkl/surface.h>
#include <vkkl/semaphore.h>

export module demo.render;

import std;
import demo.execution;
import demo.window;

export struct surface_changed {};

struct device_selection
{
	::VkPhysicalDevice handle;
	::std::uint32_t graphics_family;
	::std::uint32_t present_family;
	::std::uint32_t graphics_queue_count;
	::std::uint32_t present_queue_count;
	bool discrete;
};

export
struct renderer
{
	explicit renderer(window const& target_window);

	struct frame_type
	{
		::vkkl::fence in_flight;
		::vkkl::command_pool primary_command_pool;
		::vkkl::command_buffer primary_command_buffer;
		::vkkl::semaphore image_available;
		::vkkl::semaphore render_finished;

		frame_type(VkDevice device, ::std::uint32_t graphics_queue_family);
	};
	struct swapchain_type
	{
		::vkkl::swapchain swapchain;
		::VkExtent2D extent;
		::vkfu::enums::format format;
		::std::vector<::VkImage> images;
		::std::vector<::vkkl::image_view> image_views;

		swapchain_type(
			::VkDevice device
			, ::VkPhysicalDevice physical_device
			, ::VkSurfaceKHR surface
			, window const& target_window
			, ::std::span<::std::uint32_t const, 2> queue_families
			, ::VkSwapchainKHR old_swapchain);
	};
	using queue_pool = ::nagisa::concurrency::resource_pool<::VkQueue, ::std::vector<::VkQueue>>;
	using frame_pool = ::nagisa::concurrency::bounded_resource_pool<frame_type*, 4>;
	using swapchain_pool = ::nagisa::concurrency::bounded_resource_pool<swapchain_type*, 4>;

	[[nodiscard]] ::VkDevice device() const noexcept { return _device.handle; }
	[[nodiscard]] ::VkPhysicalDevice physical_device() const noexcept { return _selected_physical_device.handle; }
	[[nodiscard]] ::std::uint32_t graphics_queue_family() const noexcept { return _selected_physical_device.graphics_family; }

	[[nodiscard]] values_sender_type<queue_pool::permit> acquire_graphics_queue();
	[[nodiscard]] values_sender_type<queue_pool::permit> acquire_present_queue();
	values_sender_type<swapchain_pool::permit> acquire_swapchain();
	values_sender_type<frame_pool::permit> acquire_frame();
	values_sender_type<::std::uint32_t> acquire_swapchain_image(frame_type const& frame, swapchain_type const& swapchain) const noexcept;
	values_sender_type<> render(frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index, ::std::span<::vkkl::command_buffer const> commands) const;

	values_sender_type<> submit_graphic_queue(queue_pool::permit const& queue, frame_type const& frame) const;
	values_sender_type<> submit_present_queue(queue_pool::permit const& queue, frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index) const;

	void resize(swapchain_type& swapchain) const;

	window const& _target;
	::vkkl::instance _instance;
	::vkkl::debug_utils_messenger _debug_messenger;
	::vkkl::surface _surface;
	device_selection _selected_physical_device;
	::vkkl::device _device;
	queue_pool _graphics_queue_pool;
	queue_pool _present_queue_pool;
	::std::array<swapchain_type, 1> _swapchains;
	swapchain_pool _swapchain_pool;
	::std::array<frame_type, 4> _frames;
	frame_pool _frame_pool;

};

module : private;

renderer::frame_type::frame_type(VkDevice device, ::std::uint32_t graphics_queue_family)
	: in_flight(device, ::vkfu::create_fence(device, ::vkfu::param::fence{}))
	, primary_command_pool(device, ::vkfu::create_command_pool(device, ::vkfu::param::command_pool{
		.flags = {.transient = 1, .reset_command_buffer = 1},
		.queue_family_index = graphics_queue_family,
		}))
		, primary_command_buffer([&]
			{
				auto raw_command_buffer = ::VkCommandBuffer{};
				::vkfu::allocate_command_buffers(
					device,
					::vkfu::param::command_buffer{
						.command_pool = primary_command_pool.handle,
						.level = ::vkfu::enums::command_buffer_level::primary,
						.command_buffer_count = 1,
					},
					::std::span{ &raw_command_buffer, 1u }
					);
				return ::vkkl::command_buffer(device, primary_command_pool.handle, raw_command_buffer);
			}())
	, image_available(device, ::vkfu::create_semaphore(device, ::vkfu::param::semaphore{}))
	, render_finished(device, ::vkfu::create_semaphore(device, ::vkfu::param::semaphore{}))
{
}

::vkfu::param::khr::swapchain make_swapchain_info(
	::VkPhysicalDevice physical_device
	, ::VkSurfaceKHR surface
	, window const& target_window
	, ::std::span<::std::uint32_t const, 2> queue_families
	, ::VkSwapchainKHR old_swapchain
);
::std::vector<::VkImage> get_swapchain_images(::VkDevice device, ::VkSwapchainKHR swapchain);
::std::vector<::vkkl::image_view> create_swapchain_image_views(::VkDevice device, ::std::span<::VkImage const> images, ::vkfu::enums::format format);

renderer::swapchain_type::swapchain_type(
	::VkDevice device
	, ::VkPhysicalDevice physical_device
	, ::VkSurfaceKHR surface
	, window const& target_window
	, ::std::span<::std::uint32_t const, 2> queue_families
	, ::VkSwapchainKHR old_swapchain)
	: swapchain(device, ::vkfu::khr::create_swapchain(device, ::make_swapchain_info(physical_device, surface, target_window, queue_families, old_swapchain)))
	, extent(::make_swapchain_info(physical_device, surface, target_window, queue_families, old_swapchain).image_extent)
	, format(::make_swapchain_info(physical_device, surface, target_window, queue_families, old_swapchain).image_format)
	, images(::get_swapchain_images(device, swapchain.handle))
	, image_views(::create_swapchain_image_views(device, images, format))
{
	
}

::vkkl::instance create_instance(::std::vector<char const*> enabled_extensions);
::vkkl::debug_utils_messenger create_debug_messenger(::VkInstance instance);
device_selection select_physical_device(::VkInstance instance, ::VkSurfaceKHR surface);
::vkkl::device create_device(device_selection const& selected);

::std::vector<::VkQueue> get_device_queues(::VkDevice device, ::std::uint32_t family, ::std::uint32_t count)
{
	auto queues = ::std::vector<::VkQueue>{};
	queues.reserve(count);
	for (auto index = ::std::uint32_t{}; index < count; ++index)
		queues.push_back(::vkfu::get_device_queue(device, family, index));
	return queues;
}

renderer::renderer(window const& target_window)
	: _target(target_window)
	, _instance(::create_instance(_target.required_vulkan_extensions()))
	, _debug_messenger(::create_debug_messenger(_instance.handle))
	, _surface(_instance.handle, _target.vulkan_surface(_instance.handle))
	, _selected_physical_device(::select_physical_device(_instance.handle, _surface.handle))
	, _device(::create_device(_selected_physical_device))
	, _graphics_queue_pool(::get_device_queues(_device.handle, _selected_physical_device.graphics_family, _selected_physical_device.graphics_queue_count))
	, _present_queue_pool(::get_device_queues(_device.handle, _selected_physical_device.present_family, _selected_physical_device.present_family == _selected_physical_device.graphics_family ? 0u : _selected_physical_device.present_queue_count))
	, _swapchains{
		swapchain_type{ _device.handle, _selected_physical_device.handle, _surface.handle, target_window, ::std::array{ _selected_physical_device.graphics_family, _selected_physical_device.present_family }, VK_NULL_HANDLE }
	}
	, _swapchain_pool(::std::in_place, ::std::from_range, _swapchains | ::std::views::transform([](swapchain_type& value) noexcept { return ::std::addressof(value); }))
	, _frames{
		frame_type{ _device.handle, _selected_physical_device.graphics_family },
		frame_type{ _device.handle, _selected_physical_device.graphics_family },
		frame_type{ _device.handle, _selected_physical_device.graphics_family },
		frame_type{ _device.handle, _selected_physical_device.graphics_family },
	}
	, _frame_pool(::std::in_place, ::std::from_range, _frames | ::std::views::transform([](frame_type& value) noexcept { return ::std::addressof(value); }))
{}


values_sender_type<renderer::swapchain_pool::permit> renderer::acquire_swapchain()
{
	return _swapchain_pool.acquire();
}
values_sender_type<::std::uint32_t> renderer::acquire_swapchain_image(frame_type const& frame, swapchain_type const& swapchain) const noexcept
{
	return {
		::stdexec::just()
		| ::stdexec::then([&]
			{
				// auto index = ::vkfu::khr::acquire_next_image2(_device.handle, ::vkfu::param::khr::acquire_next_image{
				// 	.swapchain = swapchain.swapchain.handle,
				// 	.timeout = (::std::numeric_limits<::std::uint64_t>::max)(),
				// 	.semaphore = frame.image_available.handle,
				// 	.fence = VK_NULL_HANDLE,
				// 	.device_mask = 1,
				// 	});
				::std::uint32_t index;
				auto const result = ::vkAcquireNextImageKHR(
					_device.handle
					, swapchain.swapchain.handle
					, (::std::numeric_limits<::std::uint64_t>::max)()
					, frame.image_available.handle
					, VK_NULL_HANDLE
					, &index
					);
				if (result == VK_ERROR_OUT_OF_DATE_KHR)
					throw surface_changed{};
				if (result != VK_SUBOPTIMAL_KHR)
					if (result != VK_SUCCESS)
						throw ::std::runtime_error{ "acquire swapchain image" };
				return index;
			})
	};
}
values_sender_type<renderer::frame_pool::permit> renderer::acquire_frame()
{
	return _frame_pool.acquire();
}

values_sender_type<> renderer::render(frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index, ::std::span<::vkkl::command_buffer const> commands) const
{
	return {
		::stdexec::just()
		| ::stdexec::then([this, &frame, &swapchain, image_index, commands]
			{
				::vkfu::reset_command_pool(_device.handle, frame.primary_command_pool.handle, {});
				::vkfu::begin_command_buffer(frame.primary_command_buffer.handle, ::vkfu::param::command_buffer_begin{ .flags = {.one_time_submit = 1}, });
				::vkfu::cmd_pipeline_barrier2(frame.primary_command_buffer.handle, ::vkfu::param::dependency{ .image_memory_barriers = ::std::array{ ::vkfu::evaluate(::vkfu::param::image_memory_barrier2{
					.dst_stage_mask = {.color_attachment_output = 1},
					.dst_access_mask = {.color_attachment_write = 1},
					.old_layout = ::vkfu::enums::image_layout::undefined,
					.new_layout = ::vkfu::enums::image_layout::color_attachment_optimal,
					.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.image = swapchain.images[image_index],
					.subresource_range = {
						.aspect_mask = { .color = true },
						.base_mip_level = 0,
						.level_count = 1,
						.base_array_layer = 0,
						.layer_count = 1,
					},
					})},});
				::vkfu::cmd_begin_rendering(frame.primary_command_buffer.handle, ::vkfu::param::rendering<>{
					.flags = { .contents_secondary_command_buffers = 1 },
					.render_area = ::VkRect2D{ .offset = {}, .extent = swapchain.extent },
					.layer_count = 1,
					.color_attachments = ::std::array{ ::vkfu::evaluate(::vkfu::param::rendering_attachment{
						.image_view = swapchain.image_views[image_index].handle,
						.image_layout = ::vkfu::enums::image_layout::color_attachment_optimal,
						.load_op = ::vkfu::enums::attachment_load_op::clear,
						.store_op = ::vkfu::enums::attachment_store_op::store,
						.clear_value = ::VkClearValue{},
						}) },
				});
				if (!commands.empty())
				{
					auto const handles = commands | ::std::views::transform([](::vkkl::command_buffer const& command) { return command.handle; }) | ::std::ranges::to<::std::vector>();
					::vkfu::cmd_execute_commands(frame.primary_command_buffer.handle, handles);
				}
				::vkfu::cmd_end_rendering(frame.primary_command_buffer.handle);

				::vkfu::cmd_pipeline_barrier2(frame.primary_command_buffer.handle, ::vkfu::param::dependency{ .image_memory_barriers = ::std::array{ ::vkfu::evaluate(::vkfu::param::image_memory_barrier2{
					.src_stage_mask = {.color_attachment_output = 1},
					.src_access_mask = {.color_attachment_write = 1},
					.old_layout = ::vkfu::enums::image_layout::color_attachment_optimal,
					.new_layout = ::vkfu::enums::image_layout::present_src,
					.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.image = swapchain.images[image_index],
					.subresource_range = {
						.aspect_mask = {.color = true },
						.base_mip_level = 0,
						.level_count = 1,
						.base_array_layer = 0,
						.layer_count = 1,
					},
					})}, });
				::vkfu::end_command_buffer(frame.primary_command_buffer.handle);

			})
	};
}

void renderer::resize(swapchain_type& swapchain) const
{
	auto replacement = swapchain_type{
		_device.handle,
		_selected_physical_device.handle,
		_surface.handle,
		_target,
		::std::array{ _selected_physical_device.graphics_family, _selected_physical_device.present_family },
		swapchain.swapchain.handle,
	};
	// 先销毁旧视图，再由赋值释放旧交换链。
	swapchain.image_views.clear();
	swapchain = ::std::move(replacement);
}

values_sender_type<renderer::queue_pool::permit> renderer::acquire_graphics_queue()
{
	return _graphics_queue_pool.acquire();
}

values_sender_type<renderer::queue_pool::permit> renderer::acquire_present_queue()
{
	if (_selected_physical_device.present_family == _selected_physical_device.graphics_family)
		return _graphics_queue_pool.acquire();
	return _present_queue_pool.acquire();
}

values_sender_type<> renderer::submit_graphic_queue(queue_pool::permit const& queue, frame_type const& frame) const
{
	return {
		::stdexec::just()
		| ::stdexec::then([this, &queue, &frame]
			{
				::vkfu::queue_submit2(queue.token(), ::std::array{ ::vkfu::evaluate(::vkfu::param::submit2{
					.wait_semaphore_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::semaphore_submit{
						.semaphore = frame.image_available.handle, 
						.stage_mask = { .color_attachment_output = 1 },
					}) },
					.command_buffer_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::command_buffer_submit{
						.command_buffer = frame.primary_command_buffer.handle,
					}) },
					.signal_semaphore_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::semaphore_submit{
						.semaphore = frame.render_finished.handle, 
						.stage_mask = { .all_commands = 1 },
					}) },
					}) }, frame.in_flight.handle);
				::vkfu::wait_for_fences(_device.handle, ::std::array{ frame.in_flight.handle }, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());
				::vkfu::reset_fences(_device.handle, ::std::array{ frame.in_flight.handle });
			})
	};
}

values_sender_type<> renderer::submit_present_queue(queue_pool::permit const& queue, frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index) const
{
	return {
		::stdexec::just()
		| ::stdexec::then([&queue, &frame, &swapchain, image_index]
			{
				::vkfu::khr::queue_present(queue.token(), ::vkfu::param::khr::present{
					.wait_semaphores = ::std::array{ frame.render_finished.handle },
					.swapchain_count = 1,
					.swapchains = &swapchain.swapchain.handle,
					.image_indices = &image_index,
				});
				::vkfu::queue_wait_idle(queue.token());
			})
	};
}
