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

// 队列按"能做什么"而不是按族区分：presentable 的图形队列提交完可以直接呈现，不必归还再取。
// 放在类外：resource_pool 要求令牌可默认构造，嵌套类的默认成员初始化器要等外层类完成才可用。
export struct render_queue
{
	::VkQueue handle = VK_NULL_HANDLE;
	bool presentable = false;
};

export
struct renderer
{
	explicit renderer(window const& target_window);
	// 提交不再同步等待，析构前要等 GPU 用完帧与交换链。
	~renderer();

	struct frame_type
	{
		::vkkl::fence in_flight;
		::vkkl::command_pool primary_command_pool;
		::vkkl::command_buffer primary_command_buffer;
		::vkkl::semaphore image_available;

		frame_type(VkDevice device, ::std::uint32_t graphics_queue_family);
	};
	struct swapchain_type
	{
		::vkkl::swapchain swapchain;
		::VkExtent2D extent;
		::vkfu::enums::format format;
		::std::vector<::VkImage> images;
		::std::vector<::vkkl::image_view> image_views;
		// 按图像而不是按帧：present 对它的等待不受帧的 fence 覆盖，只有"该图像被再次 acquire"
		// 才说明上一次 present 已经消费了它。
		::std::vector<::vkkl::semaphore> render_finished;

		swapchain_type(
			::VkDevice device
			, ::VkPhysicalDevice physical_device
			, ::VkSurfaceKHR surface
			, window const& target_window
			, ::std::span<::std::uint32_t const, 2> queue_families
			, ::VkSwapchainKHR old_swapchain);
	};
	using queue_type = render_queue;
	using queue_pool = ::nagisa::concurrency::resource_pool<queue_type, ::std::vector<queue_type>>;
	// 场景录制的二级命令缓冲，交给 render 在主命令缓冲里执行。
	using command_view = ::std::span<::vkkl::command_buffer const>;
	using frame_pool = ::nagisa::concurrency::bounded_resource_pool<frame_type*, 4>;
	using swapchain_pool = ::nagisa::concurrency::bounded_resource_pool<swapchain_type*, 4>;

	[[nodiscard]] ::VkDevice device() const noexcept { return _device.handle; }
	[[nodiscard]] ::VkPhysicalDevice physical_device() const noexcept { return _selected_physical_device.handle; }
	[[nodiscard]] ::std::uint32_t graphics_queue_family() const noexcept { return _selected_physical_device.graphics_family; }

	[[nodiscard]] values_sender_type<queue_pool::permit> acquire_graphics_queue();
	// 交出提交过图形工作的队列，换一条能呈现的队列：
	// 图形族能呈现（同族）时原样还回同一条队列，不经过池；否则先归还它，再从呈现族的池里取。
	// 于是同族单队列也不会自锁，异族时图形队列在呈现前就已释放。
	[[nodiscard]] values_sender_type<queue_pool::permit> acquire_present_queue(queue_pool::permit graphics_queue);
	values_sender_type<swapchain_pool::permit> acquire_swapchain();
	values_sender_type<frame_pool::permit> acquire_frame();
	// 以下都是同步函数，在调用线程上执行；放到哪个调度器上由调用方决定。
	// 阻塞到该帧上一次提交的 GPU 工作完成：之后才能重用它的命令缓冲与信号量。
	void wait_frame(frame_type const& frame) const;
	// 阻塞到有可用图像；要求已 wait_frame（image_available 不能还有未完成的等待）。
	[[nodiscard]] ::std::uint32_t acquire_swapchain_image(frame_type const& frame, swapchain_type const& swapchain) const;
	void render(frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index, command_view commands) const;

	// 只提交、不等待：图形提交以 frame.in_flight 为完成信号，由下一次使用该帧时的 wait_frame 等待。
	void submit_graphic_queue(queue_pool::permit const& queue, frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index) const;
	void submit_present_queue(queue_pool::permit const& queue, frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index) const;

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
	// 初始即触发：第一次 wait_frame 直接返回。
	: in_flight(device, ::vkfu::create_fence(device, ::vkfu::param::fence{ .flags = {.signaled = 1} }))
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
	, render_finished(::std::from_range, images | ::std::views::transform([device](::VkImage) { return ::vkkl::semaphore(device, ::vkfu::create_semaphore(device, ::vkfu::param::semaphore{})); }))
{
}

::vkkl::instance create_instance(::std::vector<char const*> enabled_extensions);
::vkkl::debug_utils_messenger create_debug_messenger(::VkInstance instance);
device_selection select_physical_device(::VkInstance instance, ::VkSurfaceKHR surface);
::vkkl::device create_device(device_selection const& selected);

::std::vector<renderer::queue_type> get_device_queues(::VkDevice device, ::std::uint32_t family, ::std::uint32_t count, bool presentable)
{
	auto queues = ::std::vector<renderer::queue_type>{};
	queues.reserve(count);
	for (auto index = ::std::uint32_t{}; index < count; ++index)
		queues.push_back({ .handle = ::vkfu::get_device_queue(device, family, index), .presentable = presentable });
	return queues;
}

renderer::renderer(window const& target_window)
	: _target(target_window)
	, _instance(::create_instance(_target.required_vulkan_extensions()))
	, _debug_messenger(::create_debug_messenger(_instance.handle))
	, _surface(_instance.handle, _target.vulkan_surface(_instance.handle))
	, _selected_physical_device(::select_physical_device(_instance.handle, _surface.handle))
	, _device(::create_device(_selected_physical_device))
	// 同族时只有一个池，呈现池为空，acquire_present_queue 直接复用图形队列。
	, _graphics_queue_pool(::get_device_queues(_device.handle, _selected_physical_device.graphics_family, _selected_physical_device.graphics_queue_count, _selected_physical_device.present_family == _selected_physical_device.graphics_family))
	, _present_queue_pool(::get_device_queues(_device.handle, _selected_physical_device.present_family, _selected_physical_device.present_family == _selected_physical_device.graphics_family ? 0u : _selected_physical_device.present_queue_count, true))
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

renderer::~renderer()
{
	if (_device.handle != VK_NULL_HANDLE)
		::vkfu::device_wait_idle(_device.handle);
}

values_sender_type<renderer::swapchain_pool::permit> renderer::acquire_swapchain()
{
	return _swapchain_pool.acquire();
}
void renderer::wait_frame(frame_type const& frame) const
{
	::vkfu::wait_for_fences(_device.handle, ::std::array{ frame.in_flight.handle }, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());
}

::std::uint32_t renderer::acquire_swapchain_image(frame_type const& frame, swapchain_type const& swapchain) const
{
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
	if (result != VK_SUBOPTIMAL_KHR && result != VK_SUCCESS)
		throw ::std::runtime_error{ "acquire swapchain image" };
	return index;
}
values_sender_type<renderer::frame_pool::permit> renderer::acquire_frame()
{
	return _frame_pool.acquire();
}

void renderer::render(frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index, command_view commands) const
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
}

void renderer::resize(swapchain_type& swapchain) const
{
	// 提交与呈现都不再同步等待：旧交换链的图像可能仍被在途帧使用。
	::vkfu::device_wait_idle(_device.handle);
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
	swapchain.render_finished.clear();
	swapchain = ::std::move(replacement);
}

values_sender_type<renderer::queue_pool::permit> renderer::acquire_graphics_queue()
{
	return _graphics_queue_pool.acquire();
}

values_sender_type<renderer::queue_pool::permit> renderer::acquire_present_queue(queue_pool::permit graphics_queue)
{
	if (graphics_queue.token().presentable)
		return ::stdexec::just(::std::move(graphics_queue));
	// 先归还图形队列再排队等呈现队列：不同时持有两种队列，交叉等待就不会死锁。
	graphics_queue = {};
	return _present_queue_pool.acquire();
}

void renderer::submit_graphic_queue(queue_pool::permit const& queue, frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index) const
{
	// in_flight 由 wait_frame 等到触发，这里在提交前复位；GPU 完成本次提交后再次触发它。
	::vkfu::reset_fences(_device.handle, ::std::array{ frame.in_flight.handle });
	::vkfu::queue_submit2(queue.token().handle, ::std::array{ ::vkfu::evaluate(::vkfu::param::submit2{
		.wait_semaphore_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::semaphore_submit{
			.semaphore = frame.image_available.handle,
			.stage_mask = { .color_attachment_output = 1 },
		}) },
		.command_buffer_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::command_buffer_submit{
			.command_buffer = frame.primary_command_buffer.handle,
		}) },
		.signal_semaphore_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::semaphore_submit{
			.semaphore = swapchain.render_finished[image_index].handle,
			.stage_mask = { .all_commands = 1 },
		}) },
		}) }, frame.in_flight.handle);
}

void renderer::submit_present_queue(queue_pool::permit const& queue, frame_type const& frame, swapchain_type const& swapchain, ::std::uint32_t image_index) const
{
	// 次优 / 过期都交给调用方重建交换链；呈现本身已入队（过期时没有）。
	assert(queue.token().presentable);
	auto const outcome = ::vkfu::khr::queue_present(queue.token().handle, ::vkfu::param::khr::present{
		.wait_semaphores = ::std::array{ swapchain.render_finished[image_index].handle },
		.swapchain_count = 1,
		.swapchains = &swapchain.swapchain.handle,
		.image_indices = &image_index,
	}, ::std::nothrow);
	if (outcome)
		return;
	if (outcome.error() == VK_ERROR_OUT_OF_DATE_KHR || outcome.error() == VK_SUBOPTIMAL_KHR)
		throw surface_changed{};
	throw ::std::runtime_error{ "present swapchain image" };
}
