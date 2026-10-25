module;

// 只包含真正用到的 vkfu 头，而不是囊括全部厂商扩展的 <vkfu/vulkan/v1.4.328.h>：
// 这是本实现单元最大的一块解析开销。
#include <vkfu/vulkan/v1.4.328/version_1_0.h>
#include <vkfu/vulkan/v1.4.328/version_1_1.h>
#include <vkfu/vulkan/v1.4.328/version_1_3.h>
#include <vkfu/vulkan/v1.4.328/khr/swapchain.h>
#include <vkfu/vulkan/v1.4.328/ext/debug_utils.h>
#include <vkfu/vulkan/v1.4.328/layer/names.h>

#include <vkkl/instance.h>
#include <vkkl/device.h>
#include <vkkl/physical_device.h>
#include <vkkl/swapchain.h>
#include <vkkl/image_view.h>
#include <vkkl/surface.h>
#include <vkkl/semaphore.h>
#include <cassert>

#include <vkkl/command_buffer.h>

module demo.render;

// 实现单元隐式导入 demo.render 的接口；接口 import 的 std 与 demo.execution 在这里同样可见，
// 所以 sender 算法直接用 demo.execution 导出的 just / then，不再包含 stdexec 头文件。

inline void check(::VkResult result, char const* operation)
{
	if (result != ::VK_SUCCESS)
	{
		throw ::std::runtime_error{ operation };
	}
}

inline ::std::vector<::std::uint32_t> read_spirv(char const* path)
{
	auto file = ::std::ifstream{ path, ::std::ios::binary | ::std::ios::ate };
	if (!file)
	{
		throw ::std::runtime_error{ "failed to open SPIR-V shader" };
	}

	auto const end = file.tellg();
	if (end <= 0 || end % static_cast<::std::streamoff>(sizeof(::std::uint32_t)) != 0)
	{
		throw ::std::runtime_error{ "invalid SPIR-V shader size" };
	}

	auto words = ::std::vector<::std::uint32_t>(static_cast<::std::size_t>(end) / sizeof(::std::uint32_t));
	file.seekg(0, ::std::ios::beg);
	if (!file.read(reinterpret_cast<char*>(words.data()), end))
	{
		throw ::std::runtime_error{ "failed to read SPIR-V shader" };
	}
	return words;
}

inline bool has_layer(::std::span<::VkLayerProperties const> available, ::std::string_view wanted)
{
	return ::std::ranges::any_of(available, [wanted](::VkLayerProperties const& entry)
		{
			return ::std::string_view{ entry.layerName } == wanted;
		});
}

inline bool has_extension(::std::span<::VkExtensionProperties const> available, ::std::string_view wanted)
{
	return ::std::ranges::any_of(available, [wanted](::VkExtensionProperties const& entry)
		{
			return ::std::string_view{ entry.extensionName } == wanted;
		});
}

inline ::std::vector<::VkLayerProperties> instance_layers()
{
	auto count = ::std::uint32_t{};
	::check(::vkEnumerateInstanceLayerProperties(&count, nullptr), "failed to count instance layers");
	auto layers = ::std::vector<::VkLayerProperties>(count);
	::check(::vkEnumerateInstanceLayerProperties(&count, layers.data()), "failed to enumerate instance layers");
	layers.resize(count);
	return layers;
}

inline ::std::vector<::VkExtensionProperties> device_extensions(::VkPhysicalDevice device)
{
	auto count = ::std::uint32_t{};
	::check(::vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr), "failed to count device extensions");
	auto extensions = ::std::vector<::VkExtensionProperties>(count);
	::check(::vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()), "failed to enumerate device extensions");
	extensions.resize(count);
	return extensions;
}

inline VKAPI_ATTR ::VkBool32 VKAPI_CALL report_debug_message(::VkDebugUtilsMessageSeverityFlagBitsEXT, ::VkDebugUtilsMessageTypeFlagsEXT, ::VkDebugUtilsMessengerCallbackDataEXT const* data, void*)
{
	if (data != nullptr && data->pMessage != nullptr)
	{
		::std::fputs(data->pMessage, stderr);
		::std::fputc('\n', stderr);
	}
	return VK_FALSE;
}


inline ::vkkl::instance create_instance(::std::vector<char const*> enabled_extensions)
{
	namespace param = ::vkfu::param;

	auto const layers = ::instance_layers();
	auto const validation = ::has_layer(layers, ::vkfu::layer::khronos_validation);

	auto enabled_layers = ::std::vector<char const*>{};
	if (validation)
	{
		enabled_layers.push_back(::vkfu::layer::khronos_validation);
		enabled_extensions.push_back(::vkfu::ext::debug_utils_extension_name);
	}

	return { ::vkfu::create_instance(param::instance{
		.application_info = param::application{
			.name = "bvn consumer architecture triangle",
			.version = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.engine_name = "bvn",
			.engine_version = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.api_version = VK_API_VERSION_1_3,
		},
		.enabled_layer_names = enabled_layers,
		.enabled_extension_names = enabled_extensions,
		}) };

}

inline ::vkkl::debug_utils_messenger create_debug_messenger(::VkInstance instance)
{
	if (!::has_layer(::instance_layers(), ::vkfu::layer::khronos_validation))
		return {};
	return ::vkkl::instance_observer{ instance }.create_debug_utils_messenger_ext(
		::VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | ::VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		::VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | ::VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | ::VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		&::report_debug_message);
}

inline device_selection select_physical_device(::VkInstance instance, ::VkSurfaceKHR surface)
{
	auto device_count = ::std::uint32_t{};
	::check(::vkEnumeratePhysicalDevices(instance, &device_count, nullptr), "failed to count physical devices");
	auto candidates = ::std::vector<::VkPhysicalDevice>(device_count);
	::check(::vkEnumeratePhysicalDevices(instance, &device_count, candidates.data()), "failed to enumerate physical devices");
	candidates.resize(device_count);

	auto best = ::std::optional<device_selection>{};

	for (auto const candidate : candidates)
	{
		auto properties = ::VkPhysicalDeviceProperties{};
		::vkGetPhysicalDeviceProperties(candidate, &properties);
		if (properties.apiVersion < VK_API_VERSION_1_3)
			continue;
		if (!::has_extension(::device_extensions(candidate), ::vkfu::khr::swapchain_extension_name))
			continue;

		auto vulkan13 = ::VkPhysicalDeviceVulkan13Features{};
		vulkan13.sType = ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		auto supported = ::VkPhysicalDeviceFeatures2{};
		supported.sType = ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		supported.pNext = &vulkan13;
		::vkGetPhysicalDeviceFeatures2(candidate, &supported);
		if (vulkan13.dynamicRendering != VK_TRUE || vulkan13.synchronization2 != VK_TRUE)
		{
			continue;
		}

		auto family_count = ::std::uint32_t{};
		::vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
		auto families = ::std::vector<::VkQueueFamilyProperties>(family_count);
		::vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());

		auto graphics = ::std::optional<::std::uint32_t>{};
		auto present = ::std::optional<::std::uint32_t>{};
		for (auto index = ::std::uint32_t{}; index < family_count; ++index)
		{
			if (!graphics && (families[index].queueFlags & ::VK_QUEUE_GRAPHICS_BIT) != 0)
				graphics = index;
			auto can_present = ::VkBool32{ VK_FALSE };
			::check(::vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface, &can_present), "failed to query presentation support");
			if (!present && can_present == VK_TRUE)
				present = index;
		}
		if (!graphics || !present)
			continue;

		auto const found = device_selection{
			.handle = candidate,
			.graphics_family = *graphics,
			.present_family = *present,
			.discrete = properties.deviceType == ::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
		};
		if (!best || (found.discrete && !best->discrete))
			best = found;
	}

	if (!best)
		throw ::std::runtime_error{ "failed to select Vulkan physical device" };
	return *best;
}

inline ::vkkl::device create_device(device_selection const& selected)
{
	namespace param = ::vkfu::param;

	auto const priority = 1.0f;
	auto queues = ::std::vector<::VkDeviceQueueCreateInfo>{};
	queues.push_back(::vkfu::evaluate(param::device_queue{
		.queue_family_index = selected.graphics_family,
		.queue_priorities = ::std::span{&priority, 1u},
		}));
	if (selected.present_family != selected.graphics_family)
	{
		queues.push_back(::vkfu::evaluate(param::device_queue{
			.queue_family_index = selected.present_family,
			.queue_priorities = ::std::span{&priority, 1u},
			}));
	}
	char const* const device_extensions[]{ ::vkfu::khr::swapchain_extension_name };

	return ::vkkl::device{ ::vkfu::create_device(
		selected.handle,
		::vkfu::chain(
			param::device{
				.queue_create_infos = queues,
				.enabled_extension_names = device_extensions,
			},
			param::feature::vulkan13{
				.synchronization2 = true,
				.dynamic_rendering = true,
			}
		)
	) };
	
}
// ::vkfu::get_device_queue(device, family, 0)

inline ::vkfu::param::khr::swapchain make_swapchain_info(
	::VkPhysicalDevice physical_device
	, ::VkSurfaceKHR surface
	, window const& target_window
	, ::std::span<::std::uint32_t const, 2> queue_families
	, ::VkSwapchainKHR old_swapchain
	)
{
	using namespace ::vkfu::enums;
	auto capabilities = ::VkSurfaceCapabilitiesKHR{};
	::check(::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities), "failed to query surface capabilities");
	if ((capabilities.supportedUsageFlags & ::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
		throw ::std::runtime_error{ "surface does not support color attachments" };

	auto format_count = ::std::uint32_t{};
	::check(::vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr), "failed to count surface formats");
	auto formats = ::std::vector<::VkSurfaceFormatKHR>(format_count);
	::check(::vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data()), "failed to query surface formats");
	formats.resize(format_count);
	if (formats.empty())
		throw ::std::runtime_error{ "surface has no formats" };
	auto surface_format = formats.front();
	if (formats.size() == 1 && surface_format.format == ::VK_FORMAT_UNDEFINED)
		surface_format.format = ::VK_FORMAT_B8G8R8A8_SRGB;
	for (auto const candidate : formats)
	{
		if (candidate.format == ::VK_FORMAT_B8G8R8A8_SRGB && candidate.colorSpace == ::VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			surface_format = candidate;
			break;
		}
	}

	auto extent = capabilities.currentExtent;
	if (extent.width == (::std::numeric_limits<::std::uint32_t>::max)())
	{
		auto const drawable = target_window.drawable_extent();
		extent = {
			.width = ::std::clamp(drawable.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			.height = ::std::clamp(drawable.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
		};
	}
	if (extent.width == 0 || extent.height == 0)
		throw ::std::runtime_error{ "cannot create a swapchain for an empty surface" };

	auto image_count = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount != 0)
		image_count = ::std::min(image_count, capabilities.maxImageCount);
	auto const concurrent = queue_families[0] != queue_families[1];
	auto const alpha_modes = capabilities.supportedCompositeAlpha;
	if (alpha_modes == 0)
		throw ::std::runtime_error{ "surface has no composite alpha mode" };
	return {
		.flags = {},
		.surface = surface,
		.min_image_count = image_count,
		.image_format = static_cast<format>(surface_format.format),
		.image_color_space = static_cast<color_space>(surface_format.colorSpace),
		.image_extent = extent,
		.image_array_layers = 1,
		.image_usage = { .color_attachment = 1 },
		.image_sharing_mode = concurrent ? sharing_mode::concurrent : sharing_mode::exclusive,
		.queue_family_indices = concurrent ? ::std::span<::std::uint32_t const>{ queue_families } : ::std::span<::std::uint32_t const>{},
		.pre_transform = static_cast<surface_transform>(capabilities.currentTransform),
		.composite_alpha = static_cast<composite_alpha>(alpha_modes & (~alpha_modes + 1u)),
		.present_mode = present_mode::fifo,
		.clipped = true,
		.old_swapchain = old_swapchain,
	};
}


inline ::std::vector<::VkImage> get_swapchain_images(::VkDevice device, ::VkSwapchainKHR swapchain)
{
	for (;;)
	{
		auto count = ::std::uint32_t{};
		::check(::vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "failed to count swapchain images");
		auto images = ::std::vector<::VkImage>(count);
		auto const result = ::vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());
		if (result == ::VK_INCOMPLETE)
			continue;
		::check(result, "failed to get swapchain images");
		images.resize(count);
		return images;
	}
}

inline ::std::vector<::vkkl::image_view> create_swapchain_image_views( ::VkDevice device, ::std::span<::VkImage const> images, ::vkfu::enums::format format)
{
	auto views = ::std::vector<::vkkl::image_view>{};
	views.reserve(images.size());
	for (auto const image : images)
	{
		views.push_back(::vkkl::image_view{ device, ::vkfu::create_image_view(device, ::vkfu::param::image_view{
			.image = image,
			.view_type = ::vkfu::enums::image_view_type::dim_2d,
			.format = format,
			.subresource_range = {
				.aspect_mask = { .color = 1 },
				.base_mip_level = 0,
				.level_count = 1,
				.base_array_layer = 0,
				.layer_count = 1,
			},
		}) });
	}
	return views;
}

void renderer::resize([[maybe_unused]] guard_type::permit const& permit) const
{
	// permit 必须来自本 entity 的 guard（比较 permit 记录的信号量地址）。
	assert(permit._semaphore == ::std::addressof(_impl->guard._semaphore));
	// 持有 permit 说明本帧之前对交换链的访问都已结束；等设备空闲，确保 GPU 端也不再引用旧图像。
	::check(::vkDeviceWaitIdle(_impl->device.handle), "wait before swapchain resize");
	auto const format = _impl->owner.info.image_format;
	// 先在副本上建好新交换链与视图，成功后再一起替换，失败时 state 与 owner 保持旧值。
	auto next = _impl->state;
	auto replacement = ::create_swapchain_owner(_impl->device.handle, _impl->physical_device.handle, _impl->surface.handle, _impl->target, _impl->queue_families,
		_impl->owner.swapchain.handle, next);
	if (replacement.info.image_format != format)
		throw ::std::runtime_error{ "surface format changed; scene pipelines must be recreated" };
	// 旧视图先于旧交换链销毁（视图引用的是旧交换链的图像）。
	_impl->owner.image_views.clear();
	_impl->owner = ::std::move(replacement);
	_impl->state = ::std::move(next);
}

values_sender_type<renderer::frame_pool::permit> renderer::acquire_frame() const
{
	return _impl->frame_pool.acquire();
}

values_sender_type<image_acquisition> renderer::acquire_image([[maybe_unused]] guard_type::permit const& permit, frame const& frame) const
{
	// permit 必须来自本 entity 的 guard（比较 permit 记录的信号量地址）。
	assert(permit._semaphore == ::std::addressof(_impl->guard._semaphore));
	return {
		::stdexec::just()
		| ::stdexec::then([this, &state = _impl->state, &frame]
			{
				auto index = ::std::uint32_t{};
				auto const result = ::vkAcquireNextImageKHR(_impl->device.handle, state.swapchain,
					(::std::numeric_limits<::std::uint64_t>::max)(), frame.image_available.handle, VK_NULL_HANDLE, &index);
				if (result == VK_ERROR_OUT_OF_DATE_KHR)
					throw surface_changed{};
				if (result != VK_SUBOPTIMAL_KHR)
					::check(result, "acquire swapchain image");
				return image_acquisition{
					.index = index,
					.semaphore = frame.image_available.handle,
					.image = state.images[index],
					.view = state.image_views[index],
					.owner = ::std::addressof(frame),
				};
			})
	};
}

values_sender_type<> renderer::render([[maybe_unused]] guard_type::permit const& permit, image_acquisition const& acquisition, ::std::span<::vkkl::command_buffer const> commands) const
{
	// permit 必须来自本 entity 的 guard（比较 permit 记录的信号量地址）。
	assert(permit._semaphore == ::std::addressof(_impl->guard._semaphore));
	return {
		::stdexec::just()
		| ::stdexec::then([this, &state = _impl->state, &frame = *acquisition.owner, &acquisition, commands]
			{
				::vkfu::reset_command_pool(_impl->device.handle, frame.primary_command_pool.handle, {});
				::vkfu::begin_command_buffer(frame.primary_command_buffer.handle, ::vkfu::param::command_buffer_begin{ .flags = {.one_time_submit = 1}, });
				::vkfu::cmd_pipeline_barrier2(frame.primary_command_buffer.handle, ::vkfu::param::dependency{ .image_memory_barriers = ::std::array{ ::vkfu::evaluate(::vkfu::param::image_memory_barrier2{
					.dst_stage_mask = {.color_attachment_output = 1},
					.dst_access_mask = {.color_attachment_write = 1},
					.old_layout = ::vkfu::enums::image_layout::undefined,
					.new_layout = ::vkfu::enums::image_layout::color_attachment_optimal,
					.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
					.image = acquisition.image,
					.subresource_range = {
						.aspect_mask = { .color = true },
						.base_mip_level = 0,
						.level_count = 1,
						.base_array_layer = 0,
						.layer_count = 1,
					},2
					})},});
				::vkfu::cmd_begin_rendering(frame.primary_command_buffer.handle, ::vkfu::param::rendering<>{
					.flags = { .contents_secondary_command_buffers = 1 },
					.render_area = ::VkRect2D{ .offset = {}, .extent = state.extent },
					.layer_count = 1,
					.color_attachments = ::std::array{ ::vkfu::evaluate(::vkfu::param::rendering_attachment{
						.image_view = acquisition.view,
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
					.image = acquisition.image,
					.subresource_range = {
						.aspect_mask = {.color = true },
						.base_mip_level = 0,
						.level_count = 1,
						.base_array_layer = 0,
						.layer_count = 1,
					},
					})}, });
				::vkfu::end_command_buffer(frame.primary_command_buffer.handle);

				::vkfu::queue_submit2(state.graphics_queue, ::std::array{ ::vkfu::evaluate(::vkfu::param::submit2{
					.wait_semaphore_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::semaphore_submit{.semaphore = frame.image_available.handle, .stage_mask = {.color_attachment_output = 1}, })},
					.command_buffer_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::command_buffer_submit{.command_buffer = frame.primary_command_buffer.handle, }) },
					.signal_semaphore_infos = ::std::array{ ::vkfu::evaluate(::vkfu::param::semaphore_submit{.semaphore = frame.render_finished.handle, .stage_mask = {.all_commands = 1}, }) },
					}) }, frame.in_flight.handle);
				auto const present_waits = ::std::array{ frame.render_finished.handle };
				auto const present_info = ::vkfu::evaluate(::vkfu::param::khr::present{
					.wait_semaphores = present_waits,
					.swapchain_count = 1, .swapchains = &state.swapchain, .image_indices = &acquisition.index });
				auto const presented = ::vkQueuePresentKHR(state.present_queue, &present_info);
				::vkfu::wait_for_fences(_impl->device.handle, ::std::array{ frame.in_flight.handle }, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());
				::vkfu::reset_fences(_impl->device.handle, ::std::array{ frame.in_flight.handle });
				::vkfu::queue_wait_idle(state.present_queue);
				if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
					throw surface_changed{};
				::check(presented, "present swapchain image");
			})
	};
}
