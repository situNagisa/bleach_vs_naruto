module;

#include <cassert>

#include <vulkan/vulkan.h>

#include <vkfu/vulkan/v1.4.328/version_1_0.h>
#include <vkfu/vulkan/v1.4.328/version_1_1.h>
#include <vkfu/vulkan/v1.4.328/version_1_3.h>
#include <vkfu/vulkan/v1.4.328/khr/swapchain.h>
#include <vkfu/vulkan/v1.4.328/khr/swapchain_maintenance1.h>
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

// 交换链过期：取图像时没有取到图像。本帧到此为止，交换链由最后一个持有它的节点重建。
export struct surface_changed {};

struct device_selection
{
	::VkPhysicalDevice handle;
	::std::uint32_t graphics_family;
	::std::uint32_t present_family;
	::std::uint32_t graphics_queue_count;
	::std::uint32_t present_queue_count;
	bool discrete;
	// 设备支持 swapchain_maintenance1 时为 KHR / EXT 扩展名，否则为空：present fence 只在支持时使用。
	char const* swapchain_maintenance;
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

	struct swapchain_type
	{
		::vkkl::swapchain swapchain;
		::VkExtent2D extent;
		::vkfu::enums::format format;
		::std::vector<::VkImage> images;
		::std::vector<::vkkl::image_view> image_views;
		// 按哪个窗口尺寸建的：与当前尺寸不符就重建（有的平台尺寸变化不报过期）。
		window_extent drawable;
		// 能同时取在手里的图像数：images.size() - minImageCount + 1。取得更多时 acquire 永远等不到图像。
		::std::size_t acquirable;

		swapchain_type(
			::VkDevice device
			, ::VkPhysicalDevice physical_device
			, ::VkSurfaceKHR surface
			, window_extent drawable
			, ::std::span<::std::uint32_t const, 2> queue_families
			, ::VkSwapchainKHR old_swapchain);
	};
	using image_index = ::std::uint32_t;
	// 交换链的一代。交换链 permit 覆盖它所有图像 permit：持有者在这一代上跑帧，
	// 等图像 permit 全部析构（用过它的帧都等完 present fence）后重建，再释放交换链 permit。
	struct swapchain_state
	{
		swapchain_type chain;
		// 图像的申请：容量是能同时取在手里的图像数，取得 permit 之后 vkAcquireNextImageKHR 一定能等到图像。
		::nagisa::concurrency::counting_semaphore_guard<> images;
		// acquire / present / release 要求交换链外部同步；同时按取得的先后排定呈现顺序。
		::nagisa::concurrency::binary_semaphore_guard access;
		// 这一代已不可用（取图像过期、呈现次优 / 过期）：持有者不再在它上面开新帧，等在途帧结束后重建。
		::stdexec::inplace_stop_source retire;

		explicit swapchain_state(swapchain_type&& chain);
	};
	// 两个 guard 不可移动，重建时原地替换整一代：optional 只为 emplace，始终有值。
	using swapchain_storage = ::std::optional<swapchain_state>;
	using image_permit = ::nagisa::concurrency::counting_semaphore_guard<>::permit;
	using access_permit = ::nagisa::concurrency::binary_semaphore_guard::permit;

	// 一份完整的帧资源。池里的帧一定空闲：帧末等到 GPU 工作与呈现都结束后 permit 才析构。
	struct frame_type
	{
		::vkkl::fence in_flight;
		// 呈现引擎用完 render_finished 时触发（swapchain_maintenance1）；不支持时不用。
		::vkkl::fence presented;
		::vkkl::command_pool primary_command_pool;
		::vkkl::command_buffer primary_command_buffer;
		::vkkl::semaphore image_available;
		::vkkl::semaphore render_finished;

		frame_type(VkDevice device, ::std::uint32_t graphics_queue_family);
	};

	using queue_type = render_queue;
	using queue_pool = ::nagisa::concurrency::resource_pool<queue_type, ::std::vector<queue_type>>;
	// 场景录制的二级命令缓冲，交给 render 在主命令缓冲里执行。
	using command_view = ::std::span<::vkkl::command_buffer const>;
	// 在途帧数 = 帧流水线里各自一次只能处理一帧的阶段数：
	//   1. 录制场景（场景状态由它自己的 guard 串行）；
	//   2. 交换链段：取图像 → 录主命令缓冲 → 提交 → 呈现，由交换链的 access 串行；
	//   3. GPU：同一队列按提交顺序执行；
	//   4. 呈现：图像交给呈现引擎直到 present fence 触发（显示器一次扫描一张）。
	// 四个阶段同时各有一帧时都不空闲；第五帧只能排在某个阶段前面，只增加延迟。
	// 帧里加入新的串行阶段（别的模块）时这个数随之加一。
	static constexpr auto frame_count = ::std::size_t{ 4 };
	using frame_pool = ::nagisa::concurrency::bounded_resource_pool<frame_type*, frame_count>;
	// 交换链 permit 覆盖它这一代的所有图像 permit：持有者在其中跑帧，等它们全部析构后重建（销毁旧的、建新的），
	// 重建是 permit 的最后一件工作。
	using swapchain_pool = ::nagisa::concurrency::bounded_resource_pool<swapchain_storage*, 1>;

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
	// 参数里的交换链都是 permit 下的交换链。acquire / present / release 还要求交换链的外部同步，由调用方保证。
	// 阻塞到取得图像；只取，不重建。过期时抛 surface_changed（没有取到图像，信号量也没有挂上操作）。
	// 调用方持有的图像不能超过 swapchain.acquirable，否则永远等不到。
	[[nodiscard]] image_index acquire_swapchain_image(frame_type const& frame, swapchain_type const& swapchain) const;
	// 交换链是否还匹配 drawable（有的平台尺寸变化不报过期）。
	[[nodiscard]] static bool fits(swapchain_type const& swapchain, window_extent drawable) noexcept;
	void render(frame_type const& frame, swapchain_type const& swapchain, image_index image, command_view commands) const;
	// 入队失败只可能是设备丢失 / 内存耗尽，不可恢复，直接终止。
	void submit_graphic_queue(queue_pool::permit const& queue, frame_type const& frame) const noexcept;
	// 呈现的队列操作一定已入队（次优 / 过期也是，此时让这一代退役），其它错误不可恢复，直接终止。
	void submit_present_queue(queue_pool::permit const& queue, frame_type const& frame, swapchain_state& swapchain, image_index image) const noexcept;
	// 阻塞到帧的 GPU 工作与呈现都结束，复位 fence：之后帧与交换链都可以归还。
	void wait_frame(frame_type const& frame) const noexcept;
	// 销毁旧交换链、建新的。只在交换链 permit 的最后调用：此时它的图像 permit 都已析构，图像上没有任何工作。
	void rebuild_swapchain(swapchain_storage& swapchain, window_extent drawable) const;

	// 取到图像却没有呈现（录制出错）时的回滚，依次调用：
	// 把图像还给交换链（swapchain_maintenance1），不支持时终止程序；
	void release_swapchain_image(swapchain_type const& swapchain, image_index image) const noexcept;
	// 取图像时挂在 image_available 上的触发没人等：提交一个只等它的空批次，以 in_flight 为完成信号；
	void drain_frame(queue_pool::permit const& queue, frame_type const& frame) const noexcept;
	// 阻塞到这个空批次完成，复位 in_flight。
	void wait_drained(frame_type const& frame) const noexcept;

	window const& _target;
	::vkkl::instance _instance;
	::vkkl::debug_utils_messenger _debug_messenger;
	::vkkl::surface _surface;
	device_selection _selected_physical_device;
	::vkkl::device _device;
	queue_pool _graphics_queue_pool;
	queue_pool _present_queue_pool;
	// 交换链扩展的设备函数，loader 不导出，经 vkGetDeviceProcAddr 取得；不支持时为空。
	::PFN_vkReleaseSwapchainImagesKHR _release_swapchain_images;
	swapchain_storage _swapchain;
	swapchain_pool _swapchain_pool;
	::std::array<frame_type, frame_count> _frames;
	frame_pool _frame_pool;

};

module : private;

renderer::frame_type::frame_type(VkDevice device, ::std::uint32_t graphics_queue_family)
	: in_flight(device, ::vkfu::create_fence(device, ::vkfu::param::fence{}))
	, presented(device, ::vkfu::create_fence(device, ::vkfu::param::fence{}))
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
	, window_extent drawable
	, ::std::span<::std::uint32_t const, 2> queue_families
	, ::VkSwapchainKHR old_swapchain
);
::std::vector<::VkImage> get_swapchain_images(::VkDevice device, ::VkSwapchainKHR swapchain);
::std::uint32_t query_min_image_count(::VkPhysicalDevice physical_device, ::VkSurfaceKHR surface);
::std::vector<::vkkl::image_view> create_swapchain_image_views(::VkDevice device, ::std::span<::VkImage const> images, ::vkfu::enums::format format);

renderer::swapchain_type::swapchain_type(
	::VkDevice device
	, ::VkPhysicalDevice physical_device
	, ::VkSurfaceKHR surface
	, window_extent drawable
	, ::std::span<::std::uint32_t const, 2> queue_families
	, ::VkSwapchainKHR old_swapchain)
	: swapchain(device, ::vkfu::khr::create_swapchain(device, ::make_swapchain_info(physical_device, surface, drawable, queue_families, old_swapchain)))
	, extent(::make_swapchain_info(physical_device, surface, drawable, queue_families, old_swapchain).image_extent)
	, format(::make_swapchain_info(physical_device, surface, drawable, queue_families, old_swapchain).image_format)
	, images(::get_swapchain_images(device, swapchain.handle))
	, image_views(::create_swapchain_image_views(device, images, format))
	, drawable(drawable)
	, acquirable(images.size() - ::query_min_image_count(physical_device, surface) + 1)
{
}

renderer::swapchain_state::swapchain_state(swapchain_type&& chain)
	: chain(::std::move(chain))
	, images(static_cast<::std::ptrdiff_t>(this->chain.acquirable))
	, access(1)
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
	, _release_swapchain_images(_selected_physical_device.swapchain_maintenance
		? reinterpret_cast<::PFN_vkReleaseSwapchainImagesKHR>(::vkGetDeviceProcAddr(_device.handle, "vkReleaseSwapchainImagesKHR"))
		: nullptr)
	, _swapchain{ ::std::in_place, swapchain_type{ _device.handle, _selected_physical_device.handle, _surface.handle, target_window.drawable_extent(), ::std::array{ _selected_physical_device.graphics_family, _selected_physical_device.present_family }, VK_NULL_HANDLE } }
	, _swapchain_pool(::std::in_place, ::std::from_range, ::std::views::single(::std::addressof(_swapchain)))
	, _frames{ [this]<::std::size_t... I>(::std::index_sequence<I...>)
		{
			return ::std::array<frame_type, frame_count>{ (static_cast<void>(I), frame_type{ _device.handle, _selected_physical_device.graphics_family })... };
		}(::std::make_index_sequence<frame_count>{}) }
	, _frame_pool(::std::in_place, ::std::from_range, _frames | ::std::views::transform([](frame_type& value) noexcept { return ::std::addressof(value); }))
{}

values_sender_type<renderer::swapchain_pool::permit> renderer::acquire_swapchain()
{
	return _swapchain_pool.acquire();
}
renderer::image_index renderer::acquire_swapchain_image(frame_type const& frame, swapchain_type const& swapchain) const
{
	auto index = image_index{};
	auto const result = ::vkAcquireNextImageKHR(
		_device.handle
		, swapchain.swapchain.handle
		, (::std::numeric_limits<::std::uint64_t>::max)()
		, frame.image_available.handle
		, VK_NULL_HANDLE
		, &index
		);
	// 次优时已经取到图像、image_available 会被触发：照常用完这一张，呈现时会再报次优。
	if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
		return index;
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
		throw surface_changed{};
	throw ::std::runtime_error{ "acquire swapchain image" };
}

bool renderer::fits(swapchain_type const& swapchain, window_extent drawable) noexcept
{
	return swapchain.drawable.width == drawable.width && swapchain.drawable.height == drawable.height;
}

void renderer::rebuild_swapchain(swapchain_storage& swapchain, window_extent drawable) const
{
	// 新交换链以旧的为 oldSwapchain 创建；emplace 先销毁旧的一代（视图、交换链、两个 guard）再放入新的。
	auto replacement = swapchain_type{
		_device.handle,
		_selected_physical_device.handle,
		_surface.handle,
		drawable,
		::std::array{ _selected_physical_device.graphics_family, _selected_physical_device.present_family },
		swapchain->chain.swapchain.handle,
	};
	swapchain->chain.image_views.clear();
	swapchain.emplace(::std::move(replacement));
}

values_sender_type<renderer::frame_pool::permit> renderer::acquire_frame()
{
	return _frame_pool.acquire();
}

void renderer::render(frame_type const& frame, swapchain_type const& swapchain, image_index image_index, command_view commands) const
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

void renderer::submit_graphic_queue(queue_pool::permit const& queue, frame_type const& frame) const noexcept
{
	::vkfu::queue_submit2(queue.token().handle, ::std::array{ ::vkfu::evaluate(::vkfu::param::submit2{
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
}

void renderer::submit_present_queue(queue_pool::permit const& queue, frame_type const& frame, swapchain_state& state, image_index image) const noexcept
{
	auto const& swapchain = state.chain;
	assert(queue.token().presentable);
	auto const present_fence = ::std::array{ frame.presented.handle };
	auto const present = ::vkfu::param::khr::present{
		.wait_semaphores = ::std::array{ frame.render_finished.handle },
		.swapchain_count = 1,
		.swapchains = &swapchain.swapchain.handle,
		.image_indices = &image,
	};
	auto const outcome = _selected_physical_device.swapchain_maintenance
		? ::vkfu::khr::queue_present(queue.token().handle, ::vkfu::chain(present, ::vkfu::param::option::khr::swapchain_present_fence{ .fences = present_fence }), ::std::nothrow)
		: ::vkfu::khr::queue_present(queue.token().handle, present, ::std::nothrow);
	if (outcome)
		return;
	// 次优 / 过期也是"队列操作已入队"：信号量被消费、present fence 会触发。
	if (outcome.error() == VK_SUBOPTIMAL_KHR || outcome.error() == VK_ERROR_OUT_OF_DATE_KHR)
	{
		state.retire.request_stop();
		return;
	}
	::std::terminate();
}

// 等齐并复位：fence 只在这里复位，复位之后帧才归还，下一次提交拿到的一定是未触发的 fence。
inline void wait_and_reset(::VkDevice device, ::std::span<::VkFence const> fences) noexcept
{
	::vkfu::wait_for_fences(device, fences, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());
	::vkfu::reset_fences(device, fences);
}

void renderer::wait_frame(frame_type const& frame) const noexcept
{
	// 不支持 present fence 时只等图形提交，无从得知呈现是否用完 render_finished：按约定不支持就不管。
	auto const fences = ::std::array{ frame.in_flight.handle, frame.presented.handle };
	::wait_and_reset(_device.handle, ::std::span{ fences }.first(_selected_physical_device.swapchain_maintenance ? 2 : 1));
}

void renderer::wait_drained(frame_type const& frame) const noexcept
{
	::wait_and_reset(_device.handle, ::std::array{ frame.in_flight.handle });
}

void renderer::release_swapchain_image(swapchain_type const& swapchain, image_index image) const noexcept
{
	if (!_release_swapchain_images)
		::std::terminate();
	auto const info = ::vkfu::evaluate(::vkfu::param::khr::release_swapchain_images{
		.swapchain = swapchain.swapchain.handle,
		.image_indices = ::std::span{ &image, 1u },
	});
	if (_release_swapchain_images(_device.handle, &info) != VK_SUCCESS)
		::std::terminate();
}

void renderer::drain_frame(queue_pool::permit const& queue, frame_type const& frame) const noexcept
{
	::vkfu::queue_submit2(queue.token().handle, ::std::array{ ::vkfu::evaluate(::vkfu::param::submit2{
		.wait_semaphore_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::semaphore_submit{
			.semaphore = frame.image_available.handle,
			.stage_mask = { .all_commands = 1 },
		}) },
		}) }, frame.in_flight.handle);
}
