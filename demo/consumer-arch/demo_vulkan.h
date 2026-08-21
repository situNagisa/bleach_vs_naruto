#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>

#include <bvn/platform/window.h>
#include <bvn/graphics/renderer.h>
#include <vkkl/vkkl.h>

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
	::vkb::Instance _bootstrap_instance;
	::vkb::Device _bootstrap_device;
	temporary_queue_synchronization _temporary_queue_synchronization;
};

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
	auto builder = ::vkb::InstanceBuilder{};
	builder.set_app_name("bvn consumer architecture triangle");
	builder.set_engine_name("bvn");
	builder.require_api_version(1, 3, 0);
	builder.enable_extensions(target_window.required_vulkan_extensions());
	builder.request_validation_layers(true);

	auto result = builder.build();
	if (!result)
	{
		throw ::std::runtime_error{"failed to create Vulkan instance"};
	}
	renderer._bootstrap_instance = result.value();
	renderer._instance = ::vkkl::instance{renderer._bootstrap_instance.instance};
	renderer._debug_messenger = ::vkkl::debug_utils_messenger{
		renderer._instance.handle,
		renderer._bootstrap_instance.debug_messenger,
	};
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
	auto features_13 = ::VkPhysicalDeviceVulkan13Features{};
	features_13.sType = ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features_13.dynamicRendering = VK_TRUE;
	features_13.synchronization2 = VK_TRUE;

	auto selector = ::vkb::PhysicalDeviceSelector{renderer._bootstrap_instance, renderer._surface.handle};
	selector.set_minimum_version(1, 3);
	selector.prefer_gpu_device_type(::vkb::PreferredDeviceType::discrete);
	selector.allow_any_gpu_device_type(true);
	selector.set_required_features_13(features_13);
	auto physical_device_result = selector.select();
	if (!physical_device_result)
	{
		throw ::std::runtime_error{"failed to select Vulkan physical device"};
	}

	auto device_result = ::vkb::DeviceBuilder{physical_device_result.value()}.build();
	if (!device_result)
	{
		throw ::std::runtime_error{"failed to create Vulkan device"};
	}
	renderer._bootstrap_device = device_result.value();
	renderer._physical_device = renderer._bootstrap_device.physical_device.physical_device;
	renderer._device = ::vkkl::device{renderer._bootstrap_device.device};

	auto graphics_queue_result = renderer._bootstrap_device.get_queue(::vkb::QueueType::graphics);
	auto graphics_queue_family_result = renderer._bootstrap_device.get_queue_index(::vkb::QueueType::graphics);
	auto present_queue_result = renderer._bootstrap_device.get_queue(::vkb::QueueType::present);
	auto present_queue_family_result = renderer._bootstrap_device.get_queue_index(::vkb::QueueType::present);
	if (!graphics_queue_result || !graphics_queue_family_result || !present_queue_result || !present_queue_family_result)
	{
		throw ::std::runtime_error{"failed to get Vulkan queues"};
	}
	renderer._graphics_queue = graphics_queue_result.value();
	renderer._graphics_queue_family = graphics_queue_family_result.value();
	renderer._present_queue = present_queue_result.value();
	renderer._present_queue_family = present_queue_family_result.value();
}

inline auto create_swapchain(vulkan_context& renderer, ::bvn::platform::window const& target_window) -> void
{
	auto const drawable_extent = target_window.drawable_extent();
	auto builder = ::vkb::SwapchainBuilder{
		renderer._physical_device,
		renderer._device.handle,
		renderer._surface.handle,
		renderer._graphics_queue_family,
		renderer._present_queue_family,
	};
	builder.set_desired_extent(::std::max(drawable_extent.width, 1u), ::std::max(drawable_extent.height, 1u));
	builder.set_desired_present_mode(::VK_PRESENT_MODE_FIFO_KHR);
	builder.set_desired_format({::VK_FORMAT_B8G8R8A8_SRGB, ::VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
	builder.add_image_usage_flags(::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

	auto result = builder.build();
	if (!result)
	{
		throw ::std::runtime_error{"failed to create Vulkan swapchain"};
	}
	auto created = result.value();
	renderer._swapchain = ::vkkl::swapchain{renderer._device.handle, created.swapchain};
	renderer._swapchain_extent = created.extent;
	renderer._swapchain_image_format = created.image_format;

	auto image_result = created.get_images();
	if (!image_result)
	{
		throw ::std::runtime_error{"failed to get Vulkan swapchain images"};
	}
	renderer._swapchain_images = image_result.value();
	renderer._swapchain_image_views.reserve(renderer._swapchain_images.size());
	renderer._swapchain_image_view_handles.reserve(renderer._swapchain_images.size());
	for (auto image : renderer._swapchain_images)
	{
		auto info = ::VkImageViewCreateInfo{};
		info.sType = ::VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = image;
		info.viewType = ::VK_IMAGE_VIEW_TYPE_2D;
		info.format = renderer._swapchain_image_format;
		info.subresourceRange.aspectMask = ::VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.layerCount = 1;
		auto view = renderer._device.create_image_view(info);
		renderer._swapchain_image_view_handles.push_back(view.handle);
		renderer._swapchain_image_views.push_back(::std::move(view));
	}
}

inline auto create_triangle_pipeline(vulkan_context& renderer) -> void
{
	auto const vertex_words = read_spirv(R"(D:\project\bvn\demo\consumer-arch\shaders\triangle.vert.spv)");
	auto const fragment_words = read_spirv(R"(D:\project\bvn\demo\consumer-arch\shaders\triangle.frag.spv)");
	auto vertex_info = ::VkShaderModuleCreateInfo{};
	vertex_info.sType = ::VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vertex_info.codeSize = vertex_words.size() * sizeof(::std::uint32_t);
	vertex_info.pCode = vertex_words.data();
	auto fragment_info = vertex_info;
	fragment_info.codeSize = fragment_words.size() * sizeof(::std::uint32_t);
	fragment_info.pCode = fragment_words.data();
	auto vertex_shader = renderer._device.create_shader_module(vertex_info);
	auto fragment_shader = renderer._device.create_shader_module(fragment_info);

	auto layout_info = ::VkPipelineLayoutCreateInfo{};
	layout_info.sType = ::VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	renderer._triangle_pipeline_layout = renderer._device.create_pipeline_layout(layout_info);

	auto stages = ::std::array<::VkPipelineShaderStageCreateInfo, 2>{};
	stages[0].sType = ::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = ::VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertex_shader.handle;
	stages[0].pName = "main";
	stages[1].sType = ::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = ::VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragment_shader.handle;
	stages[1].pName = "main";
	auto vertex_input = ::VkPipelineVertexInputStateCreateInfo{};
	vertex_input.sType = ::VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	auto input_assembly = ::VkPipelineInputAssemblyStateCreateInfo{};
	input_assembly.sType = ::VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = ::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	auto viewport = ::VkPipelineViewportStateCreateInfo{};
	viewport.sType = ::VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	auto rasterization = ::VkPipelineRasterizationStateCreateInfo{};
	rasterization.sType = ::VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization.polygonMode = ::VK_POLYGON_MODE_FILL;
	rasterization.cullMode = ::VK_CULL_MODE_NONE;
	rasterization.frontFace = ::VK_FRONT_FACE_CLOCKWISE;
	rasterization.lineWidth = 1.0f;
	auto multisample = ::VkPipelineMultisampleStateCreateInfo{};
	multisample.sType = ::VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = ::VK_SAMPLE_COUNT_1_BIT;
	auto blend_attachment = ::VkPipelineColorBlendAttachmentState{};
	blend_attachment.colorWriteMask = ::VK_COLOR_COMPONENT_R_BIT
		| ::VK_COLOR_COMPONENT_G_BIT
		| ::VK_COLOR_COMPONENT_B_BIT
		| ::VK_COLOR_COMPONENT_A_BIT;
	auto blend = ::VkPipelineColorBlendStateCreateInfo{};
	blend.sType = ::VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blend_attachment;
	auto dynamic_states = ::std::array{::VK_DYNAMIC_STATE_VIEWPORT, ::VK_DYNAMIC_STATE_SCISSOR};
	auto dynamic = ::VkPipelineDynamicStateCreateInfo{};
	dynamic.sType = ::VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = static_cast<::std::uint32_t>(dynamic_states.size());
	dynamic.pDynamicStates = dynamic_states.data();
	auto rendering = ::VkPipelineRenderingCreateInfo{};
	rendering.sType = ::VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = &renderer._swapchain_image_format;
	auto pipeline_info = ::VkGraphicsPipelineCreateInfo{};
	pipeline_info.sType = ::VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.pNext = &rendering;
	pipeline_info.stageCount = static_cast<::std::uint32_t>(stages.size());
	pipeline_info.pStages = stages.data();
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport;
	pipeline_info.pRasterizationState = &rasterization;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pColorBlendState = &blend;
	pipeline_info.pDynamicState = &dynamic;
	pipeline_info.layout = renderer._triangle_pipeline_layout.handle;
	renderer._triangle_pipeline = renderer._device.create_graphics_pipeline(pipeline_info);
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

	auto begin_info = ::VkCommandBufferBeginInfo{};
	begin_info.sType = ::VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = ::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check(::vkBeginCommandBuffer(primary_command_buffer, &begin_info), "failed to begin primary command buffer");

	auto image_barrier = ::VkImageMemoryBarrier2{};
	image_barrier.sType = ::VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	image_barrier.srcStageMask = ::VK_PIPELINE_STAGE_2_NONE;
	image_barrier.srcAccessMask = ::VK_ACCESS_2_NONE;
	image_barrier.dstStageMask = ::VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	image_barrier.dstAccessMask = ::VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	image_barrier.oldLayout = ::VK_IMAGE_LAYOUT_UNDEFINED;
	image_barrier.newLayout = ::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.image = frame.active_image();
	image_barrier.subresourceRange.aspectMask = ::VK_IMAGE_ASPECT_COLOR_BIT;
	image_barrier.subresourceRange.levelCount = 1;
	image_barrier.subresourceRange.layerCount = 1;
	auto dependency = ::VkDependencyInfo{};
	dependency.sType = ::VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &image_barrier;
	::vkCmdPipelineBarrier2(primary_command_buffer, &dependency);

	auto color_attachment = ::VkRenderingAttachmentInfo{};
	color_attachment.sType = ::VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color_attachment.imageView = frame.active_image_view();
	color_attachment.imageLayout = ::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color_attachment.loadOp = ::VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = ::VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.clearValue.color = {{0.025f, 0.035f, 0.055f, 1.0f}};
	auto rendering_info = ::VkRenderingInfo{};
	rendering_info.sType = ::VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering_info.flags = ::VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
	rendering_info.renderArea.extent = frame.extent();
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &color_attachment;
	::vkCmdBeginRendering(primary_command_buffer, &rendering_info);
}

inline auto create_secondary_command_pool(
	global_vulkan_env_renderer renderer
) -> ::vkkl::command_pool
{
	auto device = ::vkkl::device_observer{renderer.device()};
	auto pool_info = ::VkCommandPoolCreateInfo{};
	pool_info.sType = ::VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = ::VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	pool_info.queueFamilyIndex = renderer.graphics_queue_family();
	return device.create_command_pool(pool_info);
}

inline auto record_triangle(
	global_vulkan_env_renderer renderer,
	::bvn::graphics::frame_dynamic_forward_env_renderer const&,
	::vkkl::command_pool_observer secondary_command_pool
) -> ::vkkl::command_buffer
{
	auto allocate_info = ::VkCommandBufferAllocateInfo{};
	allocate_info.sType = ::VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate_info.commandPool = secondary_command_pool.handle;
	allocate_info.level = ::VK_COMMAND_BUFFER_LEVEL_SECONDARY;
	allocate_info.commandBufferCount = 1;
	auto raw_secondary = ::VkCommandBuffer{};
	check(::vkAllocateCommandBuffers(renderer.device(), &allocate_info, &raw_secondary), "failed to allocate secondary command buffer");
	auto secondary_command_buffer = ::vkkl::command_buffer{
		renderer.device(),
		secondary_command_pool.handle,
		raw_secondary,
	};

	auto const color_format = renderer.swapchain_image_format();
	auto inheritance_rendering = ::VkCommandBufferInheritanceRenderingInfo{};
	inheritance_rendering.sType = ::VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
	inheritance_rendering.colorAttachmentCount = 1;
	inheritance_rendering.pColorAttachmentFormats = &color_format;
	inheritance_rendering.rasterizationSamples = ::VK_SAMPLE_COUNT_1_BIT;
	auto inheritance = ::VkCommandBufferInheritanceInfo{};
	inheritance.sType = ::VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
	inheritance.pNext = &inheritance_rendering;
	auto begin_info = ::VkCommandBufferBeginInfo{};
	begin_info.sType = ::VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = ::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | ::VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
	begin_info.pInheritanceInfo = &inheritance;
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

	auto present_barrier = ::VkImageMemoryBarrier2{};
	present_barrier.sType = ::VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	present_barrier.srcStageMask = ::VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	present_barrier.srcAccessMask = ::VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	present_barrier.dstStageMask = ::VK_PIPELINE_STAGE_2_NONE;
	present_barrier.dstAccessMask = ::VK_ACCESS_2_NONE;
	present_barrier.oldLayout = ::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	present_barrier.newLayout = ::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	present_barrier.image = frame.active_image();
	present_barrier.subresourceRange.aspectMask = ::VK_IMAGE_ASPECT_COLOR_BIT;
	present_barrier.subresourceRange.levelCount = 1;
	present_barrier.subresourceRange.layerCount = 1;
	auto dependency = ::VkDependencyInfo{};
	dependency.sType = ::VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &present_barrier;
	::vkCmdPipelineBarrier2(primary_command_buffer, &dependency);
	check(::vkEndCommandBuffer(primary_command_buffer), "failed to end primary command buffer");

	auto const image_available = frame.image_available();
	auto const render_finished = frame.render_finished();
	auto wait_semaphore = ::VkSemaphoreSubmitInfo{};
	wait_semaphore.sType = ::VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	wait_semaphore.semaphore = image_available;
	wait_semaphore.stageMask = ::VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	auto command_buffer = ::VkCommandBufferSubmitInfo{};
	command_buffer.sType = ::VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	command_buffer.commandBuffer = primary_command_buffer;
	auto signal_semaphore = ::VkSemaphoreSubmitInfo{};
	signal_semaphore.sType = ::VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signal_semaphore.semaphore = render_finished;
	signal_semaphore.stageMask = ::VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	auto submit = ::VkSubmitInfo2{};
	submit.sType = ::VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.waitSemaphoreInfoCount = 1;
	submit.pWaitSemaphoreInfos = &wait_semaphore;
	submit.commandBufferInfoCount = 1;
	submit.pCommandBufferInfos = &command_buffer;
	submit.signalSemaphoreInfoCount = 1;
	submit.pSignalSemaphoreInfos = &signal_semaphore;
	check(::vkQueueSubmit2(renderer.graphics_queue(), 1, &submit, frame.in_flight()), "failed to submit Vulkan frame");

	auto const swapchain = renderer.swapchain();
	auto const active_image_index = frame.active_image_index();
	auto present = ::VkPresentInfoKHR{};
	present.sType = ::VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &render_finished;
	present.swapchainCount = 1;
	present.pSwapchains = &swapchain;
	present.pImageIndices = &active_image_index;
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
