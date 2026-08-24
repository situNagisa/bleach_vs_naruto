#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

#include <bvn/platform/window.h>
#include <bvn/graphics/renderer.h>
#include <vkkl/vkkl.h>
#include <vkfu/generated/vulkan-v1.4.328.h>

namespace consumer_arch_vulkan
{
inline auto check(::VkResult result, char const* operation) -> void
{
	if (result != ::VK_SUCCESS)
	{
		throw ::std::runtime_error{operation};
	}
}

inline auto read_spirv(char const* path) -> ::std::vector<::std::uint32_t>
{
	auto file = ::std::ifstream{path, ::std::ios::binary | ::std::ios::ate};
	if (!file)
	{
		throw ::std::runtime_error{"failed to open SPIR-V shader"};
	}

	auto const end = file.tellg();
	if (end <= 0 || end % static_cast<::std::streamoff>(sizeof(::std::uint32_t)) != 0)
	{
		throw ::std::runtime_error{"invalid SPIR-V shader size"};
	}

	auto words = ::std::vector<::std::uint32_t>(static_cast<::std::size_t>(end) / sizeof(::std::uint32_t));
	file.seekg(0, ::std::ios::beg);
	if (!file.read(reinterpret_cast<char*>(words.data()), end))
	{
		throw ::std::runtime_error{"failed to read SPIR-V shader"};
	}
	return words;
}

struct vulkan_context;

// TEMPORARY CODE: serializes host access to the Vulkan graphics/present queues.
// This is intentionally isolated so the queue-owner synchronization can be replaced later.
struct temporary_queue_synchronization
{
	mutable ::std::mutex _mutex;
};

struct global_vulkan_env_renderer
{
	vulkan_context const* _context = nullptr;

	auto instance() const noexcept -> ::VkInstance;
	auto physical_device() const noexcept -> ::VkPhysicalDevice;
	auto device() const noexcept -> ::VkDevice;
	auto graphics_queue() const noexcept -> ::VkQueue;
	auto graphics_queue_family() const noexcept -> ::std::uint32_t;
	auto present_queue() const noexcept -> ::VkQueue;
	auto swapchain() const noexcept -> ::VkSwapchainKHR;
	auto swapchain_extent() const noexcept -> ::VkExtent2D;
	auto swapchain_image_format() const noexcept -> ::VkFormat;
	auto swapchain_images() const noexcept -> ::std::span<::VkImage const>;
	auto swapchain_image_views() const noexcept -> ::std::span<::VkImageView const>;
	auto triangle_pipeline() const noexcept -> ::VkPipeline;
};

struct vulkan_context
{
	explicit vulkan_context(::bvn::platform::window const& target_window);
	~vulkan_context() noexcept;

	vulkan_context(vulkan_context const&) = delete;
	auto operator=(vulkan_context const&) -> vulkan_context& = delete;
	vulkan_context(vulkan_context&&) = delete;
	auto operator=(vulkan_context&&) -> vulkan_context& = delete;

	constexpr auto global_env() const noexcept -> global_vulkan_env_renderer
	{
		return {._context = this};
	}

	::vkkl::instance _instance;
	::vkkl::debug_utils_messenger _debug_messenger;
	::vkkl::surface _surface;
	::VkPhysicalDevice _physical_device = VK_NULL_HANDLE;
	::vkkl::device _device;
	::VkQueue _graphics_queue = VK_NULL_HANDLE;
	::std::uint32_t _graphics_queue_family = 0;
	::VkQueue _present_queue = VK_NULL_HANDLE;
	::std::uint32_t _present_queue_family = 0;
	::vkkl::swapchain _swapchain;
	::VkFormat _swapchain_image_format = ::VK_FORMAT_UNDEFINED;
	::VkExtent2D _swapchain_extent{};
	::std::vector<::VkImage> _swapchain_images;
	::std::vector<::vkkl::image_view> _swapchain_image_views;
	::std::vector<::VkImageView> _swapchain_image_view_handles;
	::vkkl::pipeline_layout _triangle_pipeline_layout;
	::vkkl::pipeline _triangle_pipeline;
	temporary_queue_synchronization _temporary_queue_synchronization;
};

namespace detail
{
inline auto has_layer(::std::span<::VkLayerProperties const> available, ::std::string_view wanted) -> bool
{
	return ::std::ranges::any_of(available, [wanted](::VkLayerProperties const& entry)
	{
		return ::std::string_view{entry.layerName} == wanted;
	});
}

inline auto has_extension(::std::span<::VkExtensionProperties const> available, ::std::string_view wanted) -> bool
{
	return ::std::ranges::any_of(available, [wanted](::VkExtensionProperties const& entry)
	{
		return ::std::string_view{entry.extensionName} == wanted;
	});
}

inline auto instance_layers() -> ::std::vector<::VkLayerProperties>
{
	auto count = ::std::uint32_t{};
	::vkEnumerateInstanceLayerProperties(&count, nullptr);
	auto layers = ::std::vector<::VkLayerProperties>(count);
	::vkEnumerateInstanceLayerProperties(&count, layers.data());
	return layers;
}

inline auto device_extensions(::VkPhysicalDevice device) -> ::std::vector<::VkExtensionProperties>
{
	auto count = ::std::uint32_t{};
	::vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
	auto extensions = ::std::vector<::VkExtensionProperties>(count);
	::vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
	return extensions;
}

inline VKAPI_ATTR auto VKAPI_CALL report_debug_message(
	::VkDebugUtilsMessageSeverityFlagBitsEXT,
	::VkDebugUtilsMessageTypeFlagsEXT,
	::VkDebugUtilsMessengerCallbackDataEXT const* data,
	void*) -> ::VkBool32
{
	if (data != nullptr && data->pMessage != nullptr)
	{
		::std::fputs(data->pMessage, stderr);
		::std::fputc('\n', stderr);
	}
	return VK_FALSE;
}
}

inline auto global_vulkan_env_renderer::instance() const noexcept -> ::VkInstance { return _context->_instance.handle; }
inline auto global_vulkan_env_renderer::physical_device() const noexcept -> ::VkPhysicalDevice { return _context->_physical_device; }
inline auto global_vulkan_env_renderer::device() const noexcept -> ::VkDevice { return _context->_device.handle; }
inline auto global_vulkan_env_renderer::graphics_queue() const noexcept -> ::VkQueue { return _context->_graphics_queue; }
inline auto global_vulkan_env_renderer::graphics_queue_family() const noexcept -> ::std::uint32_t { return _context->_graphics_queue_family; }
inline auto global_vulkan_env_renderer::present_queue() const noexcept -> ::VkQueue { return _context->_present_queue; }
inline auto global_vulkan_env_renderer::swapchain() const noexcept -> ::VkSwapchainKHR { return _context->_swapchain.handle; }
inline auto global_vulkan_env_renderer::swapchain_extent() const noexcept -> ::VkExtent2D { return _context->_swapchain_extent; }
inline auto global_vulkan_env_renderer::swapchain_image_format() const noexcept -> ::VkFormat { return _context->_swapchain_image_format; }
inline auto global_vulkan_env_renderer::swapchain_images() const noexcept -> ::std::span<::VkImage const> { return _context->_swapchain_images; }
inline auto global_vulkan_env_renderer::swapchain_image_views() const noexcept -> ::std::span<::VkImageView const> { return _context->_swapchain_image_view_handles; }
inline auto global_vulkan_env_renderer::triangle_pipeline() const noexcept -> ::VkPipeline { return _context->_triangle_pipeline.handle; }

// TEMPORARY CODE: keep the temporary queue synchronization out of frame resources.
[[nodiscard]] inline auto lock_temporary_queue_synchronization(global_vulkan_env_renderer renderer)
	-> ::std::unique_lock<::std::mutex>
{
	return ::std::unique_lock{renderer._context->_temporary_queue_synchronization._mutex};
}

inline auto create_instance(vulkan_context& renderer, ::bvn::platform::window const& target_window) -> void
{
	namespace param = ::vkfu::param;

	auto const layers = detail::instance_layers();
	auto const validation = detail::has_layer(layers, "VK_LAYER_KHRONOS_validation");

	auto const platform_extensions = target_window.required_vulkan_extensions();
	auto enabled_layers = ::std::vector<char const*>{};
	auto enabled_extensions = ::std::vector<char const*>(platform_extensions.begin(), platform_extensions.end());
	if (validation)
	{
		enabled_layers.push_back("VK_LAYER_KHRONOS_validation");
		enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	renderer._instance = ::vkkl::instance{::vkfu::create_instance(param::instance{
		// pApplicationInfo is a pointer member, so it takes a whole expression and
		// the storage this evaluates into owns what it points at.
		.application_info = param::application{
			.name = "bvn consumer architecture triangle",
			.version = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.engine_name = "bvn",
			.engine_version = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.api_version = VK_API_VERSION_1_3,
		},
		.enabled_layer_names = enabled_layers,
		.enabled_extension_names = enabled_extensions,
	})};

	if (!validation)
	{
		return;
	}

	auto const create_messenger = reinterpret_cast<::PFN_vkCreateDebugUtilsMessengerEXT>(
		::vkGetInstanceProcAddr(renderer._instance.handle, "vkCreateDebugUtilsMessengerEXT"));
	if (create_messenger == nullptr)
	{
		return;
	}

	auto const messenger_info = ::vkfu::evaluate(param::ext::debug_utils_messenger{
		.message_severity = {.warning = 1, .error = 1},
		.message_type = {.general = 1, .validation = 1, .performance = 1},
		.user_callback = &detail::report_debug_message,
	});
	auto raw_messenger = ::VkDebugUtilsMessengerEXT{VK_NULL_HANDLE};
	check(
		create_messenger(renderer._instance.handle, &messenger_info, nullptr, &raw_messenger),
		"failed to create the Vulkan debug messenger"
	);
	renderer._debug_messenger = ::vkkl::debug_utils_messenger{renderer._instance.handle, raw_messenger};
}

inline auto create_surface(vulkan_context& renderer, ::bvn::platform::window const& target_window) -> void
{
	auto const raw_surface = target_window.vulkan_surface(renderer._instance.handle);
	if (raw_surface == VK_NULL_HANDLE)
	{
		throw ::std::runtime_error{"failed to create Vulkan surface"};
	}
	renderer._surface = ::vkkl::surface{renderer._instance.handle, raw_surface};
}

inline auto create_device_and_queues(vulkan_context& renderer) -> void
{
	namespace param = ::vkfu::param;

	// Pick a device: Vulkan 1.3, the swapchain extension, dynamic rendering and
	// synchronization2, a graphics queue and a queue that can present here.
	// Discrete wins when there is a choice.
	auto device_count = ::std::uint32_t{};
	::vkEnumeratePhysicalDevices(renderer._instance.handle, &device_count, nullptr);
	auto candidates = ::std::vector<::VkPhysicalDevice>(device_count);
	::vkEnumeratePhysicalDevices(renderer._instance.handle, &device_count, candidates.data());

	struct choice
	{
		::VkPhysicalDevice handle = VK_NULL_HANDLE;
		::std::uint32_t graphics_family = 0;
		::std::uint32_t present_family = 0;
		bool discrete = false;
	};
	auto best = ::std::optional<choice>{};

	for (auto const candidate : candidates)
	{
		auto properties = ::VkPhysicalDeviceProperties{};
		::vkGetPhysicalDeviceProperties(candidate, &properties);
		if (properties.apiVersion < VK_API_VERSION_1_3)
		{
			continue;
		}
		if (!detail::has_extension(detail::device_extensions(candidate), VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			continue;
		}

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
			{
				graphics = index;
			}
			auto can_present = ::VkBool32{VK_FALSE};
			::vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, renderer._surface.handle, &can_present);
			if (!present && can_present == VK_TRUE)
			{
				present = index;
			}
		}
		if (!graphics || !present)
		{
			continue;
		}

		auto const found = choice{
			.handle = candidate,
			.graphics_family = *graphics,
			.present_family = *present,
			.discrete = properties.deviceType == ::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
		};
		if (!best || (found.discrete && !best->discrete))
		{
			best = found;
		}
	}

	if (!best)
	{
		throw ::std::runtime_error{"failed to select Vulkan physical device"};
	}
	renderer._physical_device = best->handle;
	renderer._graphics_queue_family = best->graphics_family;
	renderer._present_queue_family = best->present_family;

	auto const priority = 1.0f;
	auto queues = ::std::vector<::VkDeviceQueueCreateInfo>{};
	queues.push_back(::vkfu::evaluate(param::device_queue{
		.queue_family_index = best->graphics_family,
		.queue_priorities = ::std::span{&priority, 1u},
	}));
	if (best->present_family != best->graphics_family)
	{
		queues.push_back(::vkfu::evaluate(param::device_queue{
			.queue_family_index = best->present_family,
			.queue_priorities = ::std::span{&priority, 1u},
		}));
	}
	char const* const device_extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	renderer._device = ::vkkl::device{::vkfu::create_device(
		best->handle,
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
	)};

	::vkGetDeviceQueue(renderer._device.handle, best->graphics_family, 0, &renderer._graphics_queue);
	::vkGetDeviceQueue(renderer._device.handle, best->present_family, 0, &renderer._present_queue);
}

inline auto create_swapchain(vulkan_context& renderer, ::bvn::platform::window const& target_window) -> void
{
	namespace param = ::vkfu::param;
	using namespace ::vkfu::enums;

	auto capabilities = ::VkSurfaceCapabilitiesKHR{};
	check(
		::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer._physical_device, renderer._surface.handle, &capabilities),
		"failed to query Vulkan surface capabilities"
	);

	auto format_count = ::std::uint32_t{};
	::vkGetPhysicalDeviceSurfaceFormatsKHR(renderer._physical_device, renderer._surface.handle, &format_count, nullptr);
	auto formats = ::std::vector<::VkSurfaceFormatKHR>(format_count);
	::vkGetPhysicalDeviceSurfaceFormatsKHR(renderer._physical_device, renderer._surface.handle, &format_count, formats.data());
	if (formats.empty())
	{
		throw ::std::runtime_error{"the Vulkan surface reports no formats"};
	}

	auto mode_count = ::std::uint32_t{};
	::vkGetPhysicalDeviceSurfacePresentModesKHR(renderer._physical_device, renderer._surface.handle, &mode_count, nullptr);
	auto modes = ::std::vector<::VkPresentModeKHR>(mode_count);
	::vkGetPhysicalDeviceSurfacePresentModesKHR(renderer._physical_device, renderer._surface.handle, &mode_count, modes.data());

	auto surface_format = formats.front();
	for (auto const& candidate : formats)
	{
		if (candidate.format == ::VK_FORMAT_B8G8R8A8_SRGB
			&& candidate.colorSpace == ::VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			surface_format = candidate;
			break;
		}
	}

	// FIFO is the one mode always available, which is what the demo asked for.
	auto const requested_mode = ::VK_PRESENT_MODE_FIFO_KHR;
	if (::std::ranges::find(modes, requested_mode) == modes.end())
	{
		throw ::std::runtime_error{"the Vulkan surface does not support FIFO present"};
	}

	auto const drawable_extent = target_window.drawable_extent();
	auto extent = capabilities.currentExtent;
	if (extent.width == 0xFFFFFFFFu)
	{
		extent.width = ::std::clamp(
			::std::max(drawable_extent.width, 1u),
			capabilities.minImageExtent.width,
			capabilities.maxImageExtent.width
		);
		extent.height = ::std::clamp(
			::std::max(drawable_extent.height, 1u),
			capabilities.minImageExtent.height,
			capabilities.maxImageExtent.height
		);
	}

	auto image_count = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount != 0)
	{
		image_count = ::std::min(image_count, capabilities.maxImageCount);
	}

	auto const families = ::std::array{renderer._graphics_queue_family, renderer._present_queue_family};
	auto const concurrent = renderer._graphics_queue_family != renderer._present_queue_family;

	renderer._swapchain_image_format = surface_format.format;
	renderer._swapchain_extent = extent;
	renderer._swapchain = ::vkkl::swapchain{
		renderer._device.handle,
		::vkfu::khr::create_swapchain(renderer._device.handle, param::khr::swapchain{
			.surface = renderer._surface.handle,
			.min_image_count = image_count,
			.image_format = static_cast<format>(surface_format.format),
			.image_color_space = static_cast<color_space>(surface_format.colorSpace),
			.image_extent = extent,
			.image_array_layers = 1,
			.image_usage = {.color_attachment = 1},
			.image_sharing_mode = concurrent ? sharing_mode::concurrent : sharing_mode::exclusive,
			.queue_family_indices = concurrent ? ::std::span{families} : ::std::span<::std::uint32_t const>{},
			.pre_transform = static_cast<surface_transform>(capabilities.currentTransform),
			.composite_alpha = composite_alpha::opaque,
			.present_mode = static_cast<present_mode>(requested_mode),
			.clipped = true,
		}),
	};

	auto swapchain_image_count = ::std::uint32_t{};
	check(
		::vkGetSwapchainImagesKHR(renderer._device.handle, renderer._swapchain.handle, &swapchain_image_count, nullptr),
		"failed to count Vulkan swapchain images"
	);
	renderer._swapchain_images.resize(swapchain_image_count);
	check(
		::vkGetSwapchainImagesKHR(
			renderer._device.handle,
			renderer._swapchain.handle,
			&swapchain_image_count,
			renderer._swapchain_images.data()
		),
		"failed to get Vulkan swapchain images"
	);

	renderer._swapchain_image_views.reserve(renderer._swapchain_images.size());
	renderer._swapchain_image_view_handles.reserve(renderer._swapchain_images.size());
	for (auto image : renderer._swapchain_images)
	{
		auto info = ::vkfu::evaluate(param::image_view{
			.image = image,
			.view_type = image_view_type::dim_2d,
			.format = static_cast<format>(renderer._swapchain_image_format),
			.subresource_range = {
				.aspectMask = ::VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		});
		auto view = renderer._device.create_image_view(info);
		renderer._swapchain_image_view_handles.push_back(view.handle);
		renderer._swapchain_image_views.push_back(::std::move(view));
	}
}

inline auto create_triangle_pipeline(vulkan_context& renderer) -> void
{
	auto const vertex_words = read_spirv(R"(D:\project\bvn\demo\consumer-arch\shaders\triangle.vert.spv)");
	auto const fragment_words = read_spirv(R"(D:\project\bvn\demo\consumer-arch\shaders\triangle.frag.spv)");
	auto vertex_shader = renderer._device.create_shader_module(::vkfu::unpack(::vkfu::evaluate(::vkfu::param::shader_module{
		.code_size = vertex_words.size() * sizeof(::std::uint32_t),
		.code = vertex_words.data(),
		})));
	auto fragment_shader = renderer._device.create_shader_module(::vkfu::unpack(::vkfu::evaluate(::vkfu::param::shader_module{
		.code_size = fragment_words.size() * sizeof(::std::uint32_t),
		.code = fragment_words.data(),
		})));

	auto layout_info = ::vkfu::evaluate(::vkfu::param::pipeline_layout{});
	renderer._triangle_pipeline_layout = renderer._device.create_pipeline_layout(layout_info);

	namespace param = ::vkfu::param;
	using namespace ::vkfu::enums;

	auto const stages = ::std::array{
		::vkfu::evaluate(param::state::shader_stage{
			.stage = shader_stage::vertex,
			.module = vertex_shader.handle,
			.name = "main",
		}),
		::vkfu::evaluate(param::state::shader_stage{
			.stage = shader_stage::fragment,
			.module = fragment_shader.handle,
			.name = "main",
		}),
	};
	auto const blend_attachment = ::VkPipelineColorBlendAttachmentState{
		.colorWriteMask = ::VK_COLOR_COMPONENT_R_BIT
			| ::VK_COLOR_COMPONENT_G_BIT
			| ::VK_COLOR_COMPONENT_B_BIT
			| ::VK_COLOR_COMPONENT_A_BIT,
	};
	auto const dynamic_states = ::std::array{::VK_DYNAMIC_STATE_VIEWPORT, ::VK_DYNAMIC_STATE_SCISSOR};

	// Every pipeline state is a sub-expression of the pipeline itself, so the
	// storage below owns them and wires the pointers up on its own.
	auto pipeline_storage = ::vkfu::evaluate(
		param::graphics_pipeline{
			.stage_count = static_cast<::std::uint32_t>(stages.size()),
			.stages = stages.data(),
			.vertex_input_state = param::state::vertex_input{},
			.input_assembly_state = param::state::input_assembly{
				.topology = primitive_topology::triangle_list,
			},
			// Viewport and scissor are dynamic, so the counts stand alone and
			// the arrays stay null.
			.viewport_state = param::state::viewport{
				.viewport_count = 1,
				.scissor_count = 1,
			},
			.rasterization_state = param::state::rasterization{
				.polygon_mode = polygon_mode::fill,
				.front_face = front_face::clockwise,
				.line_width = 1.0f,
			},
			.multisample_state = param::state::multisample{
				.rasterization_samples = sample_count::count_1,
			},
			.color_blend_state = param::state::color_blend{
				.attachment_count = 1,
				.attachments = &blend_attachment,
			},
			.dynamic_state = param::state::dynamic{
				.states = dynamic_states,
			},
			.layout = renderer._triangle_pipeline_layout.handle,
		}
		| param::option::pipeline_rendering{
			.color_attachment_formats = ::std::span{&renderer._swapchain_image_format, 1u},
		}
	);
	renderer._triangle_pipeline = renderer._device.create_graphics_pipeline(::vkfu::unpack(pipeline_storage));
}

inline vulkan_context::vulkan_context(::bvn::platform::window const& target_window)
{
	create_instance(*this, target_window);
	create_surface(*this, target_window);
	create_device_and_queues(*this);
	create_swapchain(*this, target_window);
	create_triangle_pipeline(*this);
}

inline vulkan_context::~vulkan_context() noexcept
{
	if (_device.handle != VK_NULL_HANDLE)
	{
		(void)::vkDeviceWaitIdle(_device.handle);
	}
}

inline auto begin_frame(
	global_vulkan_env_renderer renderer,
	::bvn::graphics::frame_dynamic_forward_env_renderer const& frame
) -> void
{
	auto const primary_command_pool = frame.primary_command_pool();
	auto const primary_command_buffer = frame.primary_command_buffer();
	auto const in_flight = frame.in_flight();
	check(::vkResetCommandPool(renderer.device(), primary_command_pool, 0), "failed to reset primary command pool");
	check(::vkResetFences(renderer.device(), 1, &in_flight), "failed to reset frame fence");

	namespace param = ::vkfu::param;
	using namespace ::vkfu::enums;

	// command_buffer_begin has a slot (pInheritanceInfo), so evaluating it gives a
	// storage rather than the structure; unpack reaches the native head.
	auto const begin_storage = ::vkfu::evaluate(param::command_buffer_begin{
		.flags = {.one_time_submit = 1},
	});
	auto&& begin_info = ::vkfu::unpack(begin_storage);
	check(::vkBeginCommandBuffer(primary_command_buffer, &begin_info), "failed to begin primary command buffer");

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
	auto const acquire_dependency = ::vkfu::evaluate(param::dependency{
		.image_memory_barriers = ::std::span{&acquire_barrier, 1u},
	});
	::vkCmdPipelineBarrier2(primary_command_buffer, &acquire_dependency);

	auto clear_value = ::VkClearValue{};
	clear_value.color = {{0.025f, 0.035f, 0.055f, 1.0f}};
	auto const color_attachment = ::vkfu::evaluate(param::rendering_attachment{
		.image_view = frame.active_image_view(),
		.image_layout = image_layout::color_attachment_optimal,
		.load_op = attachment_load_op::clear,
		.store_op = attachment_store_op::store,
		.clear_value = clear_value,
	});
	auto const rendering_storage = ::vkfu::evaluate(param::rendering{
		.flags = {.contents_secondary_command_buffers = 1},
		.render_area = ::VkRect2D{.offset = {}, .extent = frame.extent()},
		.layer_count = 1,
		.color_attachments = ::std::span{&color_attachment, 1u},
	});
	auto&& rendering_info = ::vkfu::unpack(rendering_storage);
	::vkCmdBeginRendering(primary_command_buffer, &rendering_info);
}

inline auto create_secondary_command_pool(
	global_vulkan_env_renderer renderer
) -> ::vkkl::command_pool
{
	auto device = ::vkkl::device_observer{renderer.device()};
	auto pool_info = ::vkfu::evaluate(::vkfu::param::command_pool{
		.flags = {.transient = 1},
		.queue_family_index = renderer.graphics_queue_family(),
	});
	return device.create_command_pool(pool_info);
}

inline auto record_triangle(
	global_vulkan_env_renderer renderer,
	::bvn::graphics::frame_dynamic_forward_env_renderer const&,
	::vkkl::command_pool_observer secondary_command_pool
) -> ::vkkl::command_buffer
{
	namespace param = ::vkfu::param;
	using namespace ::vkfu::enums;

	auto raw_secondary = ::VkCommandBuffer{};
	::vkfu::allocate_command_buffers(
		renderer.device(),
		param::command_buffer{
			.command_pool = secondary_command_pool.handle,
			.level = command_buffer_level::secondary,
			.command_buffer_count = 1,
		},
		::std::span{&raw_secondary, 1u}
	);
	auto secondary_command_buffer = ::vkkl::command_buffer{
		renderer.device(),
		secondary_command_pool.handle,
		raw_secondary,
	};

	// The inheritance info is a pointer member of the begin info, so it goes in
	// as a sub-expression -- pNext chain and all.
	// A span of enums keeps the native element type, so this stays a VkFormat.
	auto const color_format = renderer.swapchain_image_format();
	auto begin = param::command_buffer_begin{
		.flags = {.one_time_submit = 1, .render_pass_continue = 1},
		.inheritance_info = param::command_buffer_inheritance{}
			| param::option::command_buffer_inheritance_rendering{
				.color_attachment_formats = ::std::span{&color_format, 1u},
				.rasterization_samples = sample_count::count_1,
			},
	};
	auto const begin_storage = ::vkfu::evaluate(begin);
	auto const& begin_info = ::vkfu::unpack(begin_storage);
	check(::vkBeginCommandBuffer(secondary_command_buffer.handle, &begin_info), "failed to begin secondary command buffer");

	auto const extent = renderer.swapchain_extent();
	auto viewport = ::VkViewport{};
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	auto scissor = ::VkRect2D{};
	scissor.extent = extent;
	::vkCmdBindPipeline(secondary_command_buffer.handle, ::VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.triangle_pipeline());
	::vkCmdSetViewport(secondary_command_buffer.handle, 0, 1, &viewport);
	::vkCmdSetScissor(secondary_command_buffer.handle, 0, 1, &scissor);
	::vkCmdDraw(secondary_command_buffer.handle, 3, 1, 0, 0);
	check(::vkEndCommandBuffer(secondary_command_buffer.handle), "failed to end secondary command buffer");
	return secondary_command_buffer;
}

inline auto submit_present_frame(
	global_vulkan_env_renderer renderer,
	::bvn::graphics::frame_dynamic_forward_env_renderer const& frame,
	::std::span<::VkCommandBuffer const> secondary_commands
) -> ::VkResult
{
	auto const primary_command_buffer = frame.primary_command_buffer();
	if (!secondary_commands.empty())
	{
		::vkCmdExecuteCommands(
			primary_command_buffer,
			static_cast<::std::uint32_t>(secondary_commands.size()),
			secondary_commands.data()
		);
	}
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
	auto const present_dependency = ::vkfu::evaluate(param::dependency{
		.image_memory_barriers = ::std::span{&present_barrier, 1u},
	});
	::vkCmdPipelineBarrier2(primary_command_buffer, &present_dependency);
	check(::vkEndCommandBuffer(primary_command_buffer), "failed to end primary command buffer");

	auto const image_available = frame.image_available();
	auto const render_finished = frame.render_finished();
	auto const wait_semaphore = ::vkfu::evaluate(param::semaphore_submit{
		.semaphore = image_available,
		.stage_mask = {.color_attachment_output = 1},
	});
	auto const command_buffer = ::vkfu::evaluate(param::command_buffer_submit{
		.command_buffer = primary_command_buffer,
	});
	auto const signal_semaphore = ::vkfu::evaluate(param::semaphore_submit{
		.semaphore = render_finished,
		.stage_mask = {.all_commands = 1},
	});
	auto const submit = ::vkfu::evaluate(param::submit2{
		.wait_semaphore_infos = ::std::span{&wait_semaphore, 1u},
		.command_buffer_infos = ::std::span{&command_buffer, 1u},
		.signal_semaphore_infos = ::std::span{&signal_semaphore, 1u},
	});
	check(::vkQueueSubmit2(renderer.graphics_queue(), 1, &submit, frame.in_flight()), "failed to submit Vulkan frame");

	auto const swapchain = renderer.swapchain();
	auto const active_image_index = frame.active_image_index();
	auto const present = ::vkfu::evaluate(param::khr::present{
		.wait_semaphores = ::std::span{&render_finished, 1u},
		.swapchain_count = 1,
		.swapchains = &swapchain,
		.image_indices = &active_image_index,
	});
	return ::vkQueuePresentKHR(renderer.present_queue(), &present);
}

inline auto wait_for_frame_gpu(
	global_vulkan_env_renderer renderer,
	::bvn::graphics::frame_dynamic_forward_env_renderer const& frame
) -> void
{
	auto const in_flight = frame.in_flight();
	check(
		::vkWaitForFences(
			renderer.device(),
			1,
			&in_flight,
			VK_TRUE,
			(::std::numeric_limits<::std::uint64_t>::max)()
		),
		"failed to synchronously wait for the frame fence"
	);
	check(::vkQueueWaitIdle(renderer.present_queue()), "failed to wait for the present queue");
}

inline auto check_present_result(::VkResult result) -> void
{
	if (result != ::VK_SUCCESS && result != ::VK_SUBOPTIMAL_KHR)
	{
		throw ::std::runtime_error{"failed to present Vulkan frame"};
	}
}
}
