module;

// 只包含真正用到的 vkfu 头，而不是囊括全部厂商扩展的 <vkfu/vulkan/v1.4.328.h>：
// 这是本实现单元最大的一块解析开销。
#include <vkfu/vulkan/v1.4.328/version_1_0.h>
#include <vkfu/vulkan/v1.4.328/version_1_1.h>
#include <vkfu/vulkan/v1.4.328/version_1_3.h>
#include <vkfu/vulkan/v1.4.328/khr/swapchain.h>
#include <vkfu/vulkan/v1.4.328/khr/swapchain_maintenance1.h>
#include <vkfu/vulkan/v1.4.328/ext/swapchain_maintenance1.h>
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


::vkkl::instance create_instance(::std::vector<char const*> enabled_extensions)
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

::vkkl::debug_utils_messenger create_debug_messenger(::VkInstance instance)
{
	if (!::has_layer(::instance_layers(), ::vkfu::layer::khronos_validation))
		return {};
	return ::vkkl::instance_observer{ instance }.create_debug_utils_messenger_ext(
		::VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | ::VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		::VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | ::VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | ::VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		&::report_debug_message);
}

device_selection select_physical_device(::VkInstance instance, ::VkSurfaceKHR surface)
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
		auto const extensions = ::device_extensions(candidate);
		if (!::has_extension(extensions, ::vkfu::khr::swapchain_extension_name))
			continue;

		// present fence 只要设备扩展（KHR 或同名前身 EXT）与特性；它所依赖的实例扩展 surface_maintenance1
		// 只在查询 / 修改呈现模式时才需要，这里不用。
		auto const maintenance_name = ::has_extension(extensions, ::vkfu::khr::swapchain_maintenance_1_extension_name) ? ::vkfu::khr::swapchain_maintenance_1_extension_name
			: ::has_extension(extensions, ::vkfu::ext::swapchain_maintenance_1_extension_name) ? ::vkfu::ext::swapchain_maintenance_1_extension_name
			: nullptr;
		auto maintenance = ::VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR{};
		maintenance.sType = ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR;
		auto vulkan13 = ::VkPhysicalDeviceVulkan13Features{};
		vulkan13.sType = ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		vulkan13.pNext = maintenance_name ? &maintenance : nullptr;
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

		// 优先选同时支持图形与呈现的族：这样一条图形队列提交完可以直接拿来呈现（见 renderer::acquire_present_queue）。
		auto graphics = ::std::optional<::std::uint32_t>{};
		auto present = ::std::optional<::std::uint32_t>{};
		for (auto index = ::std::uint32_t{}; index < family_count; ++index)
		{
			auto const can_draw = (families[index].queueFlags & ::VK_QUEUE_GRAPHICS_BIT) != 0;
			auto can_present = ::VkBool32{ VK_FALSE };
			::check(::vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface, &can_present), "failed to query presentation support");
			if (can_draw && can_present == VK_TRUE)
			{
				graphics = present = index;
				break;
			}
			if (!graphics && can_draw)
				graphics = index;
			if (!present && can_present == VK_TRUE)
				present = index;
		}
		if (!graphics || !present)
			continue;

		auto const found = device_selection{
			.handle = candidate,
			.graphics_family = *graphics,
			.present_family = *present,
			.graphics_queue_count = families[*graphics].queueCount,
			.present_queue_count = families[*present].queueCount,
			.discrete = properties.deviceType == ::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
			.swapchain_maintenance = maintenance.swapchainMaintenance1 == VK_TRUE ? maintenance_name : nullptr,
		};
		if (!best || (found.discrete && !best->discrete))
			best = found;
	}

	if (!best)
		throw ::std::runtime_error{ "failed to select Vulkan physical device" };
	return *best;
}

::vkkl::device create_device(device_selection const& selected)
{
	namespace param = ::vkfu::param;

	auto const graphics_priorities = ::std::vector<float>(selected.graphics_queue_count, 1.0f);
	auto const present_priorities = ::std::vector<float>(selected.present_queue_count, 1.0f);
	auto queues = ::std::vector<::VkDeviceQueueCreateInfo>{};
	queues.push_back(::vkfu::evaluate(param::device_queue{
		.queue_family_index = selected.graphics_family,
		.queue_priorities = graphics_priorities,
		}));
	if (selected.present_family != selected.graphics_family)
	{
		queues.push_back(::vkfu::evaluate(param::device_queue{
			.queue_family_index = selected.present_family,
			.queue_priorities = present_priorities,
			}));
	}
	auto device_extensions = ::std::vector<char const*>{ ::vkfu::khr::swapchain_extension_name };
	if (selected.swapchain_maintenance)
		device_extensions.push_back(selected.swapchain_maintenance);

	auto const device = param::device{
		.queue_create_infos = queues,
		.enabled_extension_names = device_extensions,
	};
	auto const vulkan13 = param::feature::vulkan13{
		.synchronization2 = true,
		.dynamic_rendering = true,
	};
	// 扩展的特性结构只有在扩展启用时才能出现在 pNext 链里。
	if (selected.swapchain_maintenance)
		return ::vkkl::device{ ::vkfu::create_device(selected.handle, ::vkfu::chain(device, vulkan13, param::feature::khr::swapchain_maintenance1{ .enable = true })) };
	return ::vkkl::device{ ::vkfu::create_device(selected.handle, ::vkfu::chain(device, vulkan13)) };
	
}
// ::vkfu::get_device_queue(device, family, 0)

::vkfu::param::khr::swapchain make_swapchain_info(
	::VkPhysicalDevice physical_device
	, ::VkSurfaceKHR surface
	, window_extent drawable
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
		extent = {
			.width = ::std::clamp(drawable.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			.height = ::std::clamp(drawable.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
		};
	}
	if (extent.width == 0 || extent.height == 0)
		throw ::std::runtime_error{ "cannot create a swapchain for an empty surface" };

	// 能同时取得的图像数 = 图像数 - minImageCount + 1，要不少于图像 permit 的容量 frame_count。
	auto image_count = capabilities.minImageCount + static_cast<::std::uint32_t>(renderer::frame_count) - 1;
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


::std::vector<::VkImage> get_swapchain_images(::VkDevice device, ::VkSwapchainKHR swapchain)
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

::std::vector<::vkkl::image_view> create_swapchain_image_views( ::VkDevice device, ::std::span<::VkImage const> images, ::vkfu::enums::format format)
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
