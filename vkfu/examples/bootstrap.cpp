// Instance / physical device / device / swapchain bring-up with no helper
// library: everything Vulkan is handed is a vkfu expression, and everything
// vkfu is handed back is a raw handle.
//
// This is the replacement for what vk-bootstrap does in demo/consumer-arch.
// Queries (vkEnumerate* / vkGet*) stay raw -- vkfu covers the construction side.

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace param = ::vkfu::param;
using namespace ::vkfu::enums;

namespace
{
auto has_extension(::std::span<VkExtensionProperties const> available, ::std::string_view wanted) -> bool
{
	return ::std::ranges::any_of(available, [wanted](VkExtensionProperties const& entry)
	{
		return ::std::string_view{entry.extensionName} == wanted;
	});
}

auto has_layer(::std::span<VkLayerProperties const> available, ::std::string_view wanted) -> bool
{
	return ::std::ranges::any_of(available, [wanted](VkLayerProperties const& entry)
	{
		return ::std::string_view{entry.layerName} == wanted;
	});
}

auto instance_layers() -> ::std::vector<VkLayerProperties>
{
	auto count = ::std::uint32_t{};
	::vkEnumerateInstanceLayerProperties(&count, nullptr);
	auto layers = ::std::vector<VkLayerProperties>(count);
	::vkEnumerateInstanceLayerProperties(&count, layers.data());
	return layers;
}

auto device_extensions(VkPhysicalDevice device) -> ::std::vector<VkExtensionProperties>
{
	auto count = ::std::uint32_t{};
	::vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
	auto extensions = ::std::vector<VkExtensionProperties>(count);
	::vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
	return extensions;
}
}

struct debug_messenger_callbacks
{
	static VKAPI_ATTR auto VKAPI_CALL report(
		VkDebugUtilsMessageSeverityFlagBitsEXT,
		VkDebugUtilsMessageTypeFlagsEXT,
		VkDebugUtilsMessengerCallbackDataEXT const*,
		void*) -> VkBool32
	{
		return VK_FALSE;
	}
};

/// The instance, plus the debug messenger when the validation layer is present.
struct instance_bringup
{
	VkInstance handle = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
	bool validation = false;
};

auto create_instance(::std::span<char const* const> platform_extensions) -> instance_bringup
{
	auto const layers = instance_layers();
	auto const validation = has_layer(layers, "VK_LAYER_KHRONOS_validation");

	auto enabled_layers = ::std::vector<char const*>{};
	auto enabled_extensions = ::std::vector<char const*>(platform_extensions.begin(), platform_extensions.end());
	if (validation)
	{
		enabled_layers.push_back("VK_LAYER_KHRONOS_validation");
		enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	auto bringup = instance_bringup{.validation = validation};
	bringup.handle = ::vkfu::create_instance(param::instance{
		// pApplicationInfo is a pointer member, so it takes a whole expression
		// and this storage owns what it points at.
		.application_info = param::application{
			.name = "vkfu bootstrap",
			.version = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.engine_name = "bvn",
			.engine_version = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.api_version = VK_API_VERSION_1_3,
		},
		.enabled_layer_names = enabled_layers,
		.enabled_extension_names = enabled_extensions,
	});

	if (validation)
	{
		auto const create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			::vkGetInstanceProcAddr(bringup.handle, "vkCreateDebugUtilsMessengerEXT"));
		if (create != nullptr)
		{
			auto const info = ::vkfu::evaluate(param::ext::debug_utils_messenger{
				.message_severity = {.warning = 1, .error = 1},
				.message_type = {.general = 1, .validation = 1, .performance = 1},
				.user_callback = &debug_messenger_callbacks::report,
			});
			create(bringup.handle, &info, nullptr, &bringup.messenger);
		}
	}
	return bringup;
}

/// A physical device that can present to `surface`, with the queue families to
/// use. Replaces vkb::PhysicalDeviceSelector.
struct physical_device_choice
{
	VkPhysicalDevice handle = VK_NULL_HANDLE;
	::std::uint32_t graphics_family = 0;
	::std::uint32_t present_family = 0;
	bool discrete = false;
};

auto select_physical_device(VkInstance instance, VkSurfaceKHR surface) -> physical_device_choice
{
	auto count = ::std::uint32_t{};
	::vkEnumeratePhysicalDevices(instance, &count, nullptr);
	auto devices = ::std::vector<VkPhysicalDevice>(count);
	::vkEnumeratePhysicalDevices(instance, &count, devices.data());

	auto best = ::std::optional<physical_device_choice>{};
	for (auto const device : devices)
	{
		auto properties = VkPhysicalDeviceProperties{};
		::vkGetPhysicalDeviceProperties(device, &properties);
		if (properties.apiVersion < VK_API_VERSION_1_3)
		{
			continue;
		}
		if (!has_extension(device_extensions(device), VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			continue;
		}

		// Dynamic rendering and synchronization2 are what the renderer needs.
		auto vulkan13 = VkPhysicalDeviceVulkan13Features{};
		vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		auto features = VkPhysicalDeviceFeatures2{};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features.pNext = &vulkan13;
		::vkGetPhysicalDeviceFeatures2(device, &features);
		if (vulkan13.dynamicRendering != VK_TRUE || vulkan13.synchronization2 != VK_TRUE)
		{
			continue;
		}

		auto family_count = ::std::uint32_t{};
		::vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
		auto families = ::std::vector<VkQueueFamilyProperties>(family_count);
		::vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());

		auto graphics = ::std::optional<::std::uint32_t>{};
		auto present = ::std::optional<::std::uint32_t>{};
		for (auto index = ::std::uint32_t{}; index < family_count; ++index)
		{
			if (!graphics && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
			{
				graphics = index;
			}
			auto supported = VkBool32{VK_FALSE};
			::vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &supported);
			if (!present && supported == VK_TRUE)
			{
				present = index;
			}
		}
		if (!graphics || !present)
		{
			continue;
		}

		auto const candidate = physical_device_choice{
			.handle = device,
			.graphics_family = *graphics,
			.present_family = *present,
			.discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
		};
		// Prefer a discrete GPU, otherwise take the first that qualifies.
		if (!best || (candidate.discrete && !best->discrete))
		{
			best = candidate;
		}
	}

	if (!best)
	{
		throw ::std::runtime_error{"no Vulkan 1.3 device can present to this surface"};
	}
	return *best;
}

/// Replaces vkb::DeviceBuilder. One queue per distinct family.
auto create_device(physical_device_choice const& choice) -> VkDevice
{
	auto const priority = 1.0f;
	auto queues = ::std::vector<VkDeviceQueueCreateInfo>{};
	queues.push_back(::vkfu::evaluate(param::device_queue{
		.queue_family_index = choice.graphics_family,
		.queue_priorities = ::std::span{&priority, 1u},
	}));
	if (choice.present_family != choice.graphics_family)
	{
		queues.push_back(::vkfu::evaluate(param::device_queue{
			.queue_family_index = choice.present_family,
			.queue_priorities = ::std::span{&priority, 1u},
		}));
	}

	char const* const extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	return ::vkfu::create_device(
		choice.handle,
		::vkfu::chain(
			param::device{
				.queue_create_infos = queues,
				.enabled_extension_names = extensions,
			},
			param::feature::vulkan13{
				.synchronization2 = true,
				.dynamic_rendering = true,
			}));
}

/// Replaces vkb::SwapchainBuilder.
struct swapchain_bringup
{
	VkSwapchainKHR handle = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkExtent2D extent{};
	::std::vector<VkImage> images;
	::std::vector<VkImageView> views;
};

auto create_swapchain(
	VkDevice device,
	physical_device_choice const& choice,
	VkSurfaceKHR surface,
	VkExtent2D window_extent) -> swapchain_bringup
{
	auto capabilities = VkSurfaceCapabilitiesKHR{};
	::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(choice.handle, surface, &capabilities);

	auto format_count = ::std::uint32_t{};
	::vkGetPhysicalDeviceSurfaceFormatsKHR(choice.handle, surface, &format_count, nullptr);
	auto formats = ::std::vector<VkSurfaceFormatKHR>(format_count);
	::vkGetPhysicalDeviceSurfaceFormatsKHR(choice.handle, surface, &format_count, formats.data());

	auto mode_count = ::std::uint32_t{};
	::vkGetPhysicalDeviceSurfacePresentModesKHR(choice.handle, surface, &mode_count, nullptr);
	auto modes = ::std::vector<VkPresentModeKHR>(mode_count);
	::vkGetPhysicalDeviceSurfacePresentModesKHR(choice.handle, surface, &mode_count, modes.data());

	auto chosen = formats.at(0);
	for (auto const& candidate : formats)
	{
		if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB
			&& candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			chosen = candidate;
			break;
		}
	}

	// FIFO is the one mode always available; prefer mailbox when offered.
	auto present = present_mode::fifo;
	if (::std::ranges::find(modes, VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
	{
		present = present_mode::mailbox;
	}

	auto extent = capabilities.currentExtent;
	if (extent.width == 0xFFFFFFFFu)
	{
		extent.width = ::std::clamp(window_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		extent.height = ::std::clamp(window_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
	}

	auto image_count = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount != 0)
	{
		image_count = ::std::min(image_count, capabilities.maxImageCount);
	}

	auto const families = ::std::array{choice.graphics_family, choice.present_family};
	auto const concurrent = choice.graphics_family != choice.present_family;

	auto bringup = swapchain_bringup{};
	bringup.format = chosen.format;
	bringup.extent = extent;
	bringup.handle = ::vkfu::khr::create_swapchain(device, param::khr::swapchain{
		.surface = surface,
		.min_image_count = image_count,
		.image_format = static_cast<format>(chosen.format),
		.image_color_space = static_cast<color_space>(chosen.colorSpace),
		.image_extent = extent,
		.image_array_layers = 1,
		.image_usage = {.color_attachment = 1},
		.image_sharing_mode = concurrent ? sharing_mode::concurrent : sharing_mode::exclusive,
		.queue_family_indices = concurrent ? ::std::span{families} : ::std::span<::std::uint32_t const>{},
		.pre_transform = static_cast<surface_transform>(capabilities.currentTransform),
		.composite_alpha = composite_alpha::opaque,
		.present_mode = present,
		.clipped = true,
	});

	auto count = ::std::uint32_t{};
	::vkGetSwapchainImagesKHR(device, bringup.handle, &count, nullptr);
	bringup.images.resize(count);
	::vkGetSwapchainImagesKHR(device, bringup.handle, &count, bringup.images.data());

	bringup.views.reserve(count);
	for (auto const image : bringup.images)
	{
		bringup.views.push_back(::vkfu::create_image_view(device, param::image_view{
			.image = image,
			.view_type = image_view_type::dim_2d,
			.format = static_cast<format>(chosen.format),
			.subresource_range = VkImageSubresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		}));
	}
	return bringup;
}

int main()
{
	// Nothing here runs without a driver and a window; the point is that the
	// whole bring-up path type-checks against the generated header.
	char const* const platform_extensions[]{VK_KHR_SURFACE_EXTENSION_NAME};
	auto const instance = create_instance(platform_extensions);
	auto const surface = VkSurfaceKHR{VK_NULL_HANDLE};
	auto const choice = select_physical_device(instance.handle, surface);
	auto const device = create_device(choice);
	auto const swapchain = create_swapchain(device, choice, surface, VkExtent2D{.width = 1280, .height = 720});
	return static_cast<int>(swapchain.views.size());
}
