// Every expression demo/consumer-arch builds, expressed the way the demo
// expresses it.
//
// The demo needs SDL3 / glslang / the bvn platform layer to link, so it cannot
// be built here. This mirrors its bring-up and per-frame recording so the
// expressions themselves stay under a compiler, and asserts the native
// structures come out the way the hand-written code used to set them.

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include "../include/vkfu/generated/vulkan-v1.4.328.h"

namespace param = ::vkfu::param;
using namespace ::vkfu::enums;

namespace
{
auto colour_subresource() -> VkImageSubresourceRange
{
	return VkImageSubresourceRange{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};
}
}

// ---------------------------------------------------------------- bring-up

void test_instance_expression()
{
	auto const layers = ::std::vector<char const*>{"VK_LAYER_KHRONOS_validation"};
	auto const extensions = ::std::vector<char const*>{VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

	// pApplicationInfo is a slot, so the storage owns the application info and
	// points the instance info at it.
	auto expression = param::instance{
		.application_info = param::application{
			.name = "bvn consumer architecture triangle",
			.version = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.engine_name = "bvn",
			.engine_version = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.api_version = VK_API_VERSION_1_3,
		},
		.enabled_layer_names = layers,
		.enabled_extension_names = extensions,
	};
	auto storage = ::vkfu::evaluate(expression);
	auto&& info = ::vkfu::unpack(storage);

	assert(info.sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
	assert(info.enabledLayerCount == 1);
	assert(info.enabledExtensionCount == 2);
	assert(info.pApplicationInfo != nullptr);
	assert(info.pApplicationInfo->sType == VK_STRUCTURE_TYPE_APPLICATION_INFO);
	assert(info.pApplicationInfo->apiVersion == VK_API_VERSION_1_3);

	// The slot is owned, so a copy points at its own application info.
	auto copied = storage;
	auto&& copied_info = ::vkfu::unpack(copied);
	assert(copied_info.pApplicationInfo != info.pApplicationInfo);
	assert(copied_info.pApplicationInfo->apiVersion == VK_API_VERSION_1_3);
}

void test_debug_messenger_expression()
{
	auto const info = ::vkfu::evaluate(param::ext::debug_utils_messenger{
		.message_severity = {.warning = 1, .error = 1},
		.message_type = {.general = 1, .validation = 1, .performance = 1},
	});
	assert(info.sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
	assert(info.messageSeverity
		== (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT));
	assert(info.messageType
		== (VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT));
}

void test_device_expression()
{
	auto const priority = 1.0f;
	auto queues = ::std::vector<VkDeviceQueueCreateInfo>{};
	queues.push_back(::vkfu::evaluate(param::device_queue{
		.queue_family_index = 0,
		.queue_priorities = ::std::span{&priority, 1u},
	}));
	queues.push_back(::vkfu::evaluate(param::device_queue{
		.queue_family_index = 1,
		.queue_priorities = ::std::span{&priority, 1u},
	}));
	char const* const extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	auto storage = ::vkfu::evaluate(::vkfu::chain(
		param::device{
			.queue_create_infos = queues,
			.enabled_extension_names = extensions,
		},
		param::feature::vulkan13{
			.synchronization2 = true,
			.dynamic_rendering = true,
		}));

	auto&& info = ::std::get<0>(storage.storages);
	auto&& vulkan13 = ::std::get<1>(storage.storages);
	assert(info.queueCreateInfoCount == 2);
	assert(info.pQueueCreateInfos[1].queueFamilyIndex == 1);
	assert(info.enabledExtensionCount == 1);
	assert(info.pNext == ::vkfu::address(vulkan13));
	assert(vulkan13.synchronization2 == VK_TRUE);
	assert(vulkan13.dynamicRendering == VK_TRUE);
	assert(vulkan13.pNext == nullptr);
}

void test_swapchain_expression()
{
	auto const families = ::std::array<::std::uint32_t, 2>{0, 1};
	auto const extent = VkExtent2D{.width = 1280, .height = 720};

	auto const info = ::vkfu::evaluate(param::khr::swapchain{
		.surface = VkSurfaceKHR{VK_NULL_HANDLE},
		.min_image_count = 3,
		.image_format = format::b8g8r8a8_srgb,
		.image_color_space = color_space::srgb_nonlinear,
		.image_extent = extent,
		.image_array_layers = 1,
		.image_usage = {.color_attachment = 1},
		.image_sharing_mode = sharing_mode::concurrent,
		.queue_family_indices = ::std::span{families},
		.pre_transform = surface_transform::identity,
		.composite_alpha = composite_alpha::opaque,
		.present_mode = present_mode::fifo,
		.clipped = true,
	});

	assert(info.sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
	assert(info.minImageCount == 3);
	assert(info.imageFormat == VK_FORMAT_B8G8R8A8_SRGB);
	assert(info.imageColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
	assert(info.imageUsage == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	assert(info.imageSharingMode == VK_SHARING_MODE_CONCURRENT);
	assert(info.queueFamilyIndexCount == 2);
	assert(info.preTransform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
	assert(info.compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
	assert(info.presentMode == VK_PRESENT_MODE_FIFO_KHR);
	assert(info.clipped == VK_TRUE);
}

void test_image_view_expression()
{
	auto const info = ::vkfu::evaluate(param::image_view{
		.image = VkImage{VK_NULL_HANDLE},
		.view_type = image_view_type::dim_2d,
		.format = format::b8g8r8a8_srgb,
		.subresource_range = colour_subresource(),
	});
	assert(info.sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
	assert(info.viewType == VK_IMAGE_VIEW_TYPE_2D);
	assert(info.format == VK_FORMAT_B8G8R8A8_SRGB);
	assert(info.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
}

// ---------------------------------------------------------------- per frame

void test_begin_frame_expressions()
{
	// command_buffer_begin has a slot (pInheritanceInfo), so evaluating it gives
	// a storage; unpack reaches the native head. It is the identity for the
	// structures that have no slots.
	auto const begin_storage = ::vkfu::evaluate(param::command_buffer_begin{
		.flags = {.one_time_submit = 1},
	});
	auto&& begin_info = ::vkfu::unpack(begin_storage);
	assert(begin_info.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
	assert(begin_info.flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	assert(begin_info.pInheritanceInfo == nullptr);

	auto const acquire_barrier = ::vkfu::evaluate(param::image_memory_barrier2{
		.dst_stage_mask = {.color_attachment_output = 1},
		.dst_access_mask = {.color_attachment_write = 1},
		.old_layout = image_layout::undefined,
		.new_layout = image_layout::color_attachment_optimal,
		.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
		.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
		.image = VkImage{VK_NULL_HANDLE},
		.subresource_range = colour_subresource(),
	});
	assert(acquire_barrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
	assert(acquire_barrier.srcStageMask == VK_PIPELINE_STAGE_2_NONE);
	assert(acquire_barrier.srcAccessMask == VK_ACCESS_2_NONE);
	assert(acquire_barrier.dstStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
	assert(acquire_barrier.dstAccessMask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
	assert(acquire_barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
	assert(acquire_barrier.newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	assert(acquire_barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);

	auto const acquire_dependency = ::vkfu::evaluate(param::dependency{
		.image_memory_barriers = ::std::span{&acquire_barrier, 1u},
	});
	assert(acquire_dependency.sType == VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
	assert(acquire_dependency.imageMemoryBarrierCount == 1);
	assert(acquire_dependency.pImageMemoryBarriers == &acquire_barrier);

	auto clear_value = VkClearValue{};
	clear_value.color = {{0.025f, 0.035f, 0.055f, 1.0f}};
	auto const color_attachment = ::vkfu::evaluate(param::rendering_attachment{
		.image_view = VkImageView{VK_NULL_HANDLE},
		.image_layout = image_layout::color_attachment_optimal,
		.load_op = attachment_load_op::clear,
		.store_op = attachment_store_op::store,
		.clear_value = clear_value,
	});
	assert(color_attachment.sType == VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
	assert(color_attachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(color_attachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE);

	auto const rendering_storage = ::vkfu::evaluate(param::rendering{
		.flags = {.contents_secondary_command_buffers = 1},
		.render_area = VkRect2D{.offset = {}, .extent = VkExtent2D{.width = 1280, .height = 720}},
		.layer_count = 1,
		.color_attachments = ::std::span{&color_attachment, 1u},
	});
	auto&& rendering_info = ::vkfu::unpack(rendering_storage);
	assert(rendering_info.sType == VK_STRUCTURE_TYPE_RENDERING_INFO);
	assert(rendering_info.flags == VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT);
	assert(rendering_info.renderArea.extent.width == 1280);
	assert(rendering_info.layerCount == 1);
	assert(rendering_info.colorAttachmentCount == 1);
	assert(rendering_info.pColorAttachments == &color_attachment);
}

void test_secondary_command_buffer_expressions()
{
	auto const allocate = ::vkfu::evaluate(param::command_buffer{
		.command_pool = VkCommandPool{VK_NULL_HANDLE},
		.level = command_buffer_level::secondary,
		.command_buffer_count = 1,
	});
	assert(allocate.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
	assert(allocate.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
	assert(allocate.commandBufferCount == 1);

	// pInheritanceInfo is a slot, and the inheritance info carries its own pNext
	// chain -- so a two-level expression goes in as one value.
	// A span of enums keeps the native element type, because a span borrows and
	// re-typing the elements would mean copying them.
	auto const color_format = VK_FORMAT_B8G8R8A8_SRGB;
	auto begin = param::command_buffer_begin{
		.flags = {.one_time_submit = 1, .render_pass_continue = 1},
		.inheritance_info = param::command_buffer_inheritance{}
			| param::option::command_buffer_inheritance_rendering{
				.color_attachment_formats = ::std::span{&color_format, 1u},
				.rasterization_samples = sample_count::count_1,
			},
	};
	auto storage = ::vkfu::evaluate(begin);
	auto&& begin_info = ::vkfu::unpack(storage);

	assert(begin_info.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
	assert(begin_info.flags
		== (VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT));
	assert(begin_info.pInheritanceInfo != nullptr);
	assert(begin_info.pInheritanceInfo->sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO);

	auto const* rendering = static_cast<VkCommandBufferInheritanceRenderingInfo const*>(
		begin_info.pInheritanceInfo->pNext);
	assert(rendering != nullptr);
	assert(rendering->sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO);
	assert(rendering->colorAttachmentCount == 1);
	assert(rendering->rasterizationSamples == VK_SAMPLE_COUNT_1_BIT);
	assert(rendering->pNext == nullptr);

	// Copying the whole thing re-points the parent at its own children.
	auto copied = storage;
	auto&& copied_info = ::vkfu::unpack(copied);
	assert(copied_info.pInheritanceInfo != begin_info.pInheritanceInfo);
	assert(copied_info.pInheritanceInfo->pNext != begin_info.pInheritanceInfo->pNext);
	assert(static_cast<VkCommandBufferInheritanceRenderingInfo const*>(
		copied_info.pInheritanceInfo->pNext)->colorAttachmentCount == 1);
}

void test_submit_present_expressions()
{
	auto const present_barrier = ::vkfu::evaluate(param::image_memory_barrier2{
		.src_stage_mask = {.color_attachment_output = 1},
		.src_access_mask = {.color_attachment_write = 1},
		.old_layout = image_layout::color_attachment_optimal,
		.new_layout = image_layout::present_src,
		.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
		.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
		.image = VkImage{VK_NULL_HANDLE},
		.subresource_range = colour_subresource(),
	});
	assert(present_barrier.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	assert(present_barrier.dstStageMask == VK_PIPELINE_STAGE_2_NONE);

	auto const wait_semaphore = ::vkfu::evaluate(param::semaphore_submit{
		.semaphore = VkSemaphore{VK_NULL_HANDLE},
		.stage_mask = {.color_attachment_output = 1},
	});
	auto const command_buffer = ::vkfu::evaluate(param::command_buffer_submit{
		.command_buffer = VkCommandBuffer{VK_NULL_HANDLE},
	});
	auto const signal_semaphore = ::vkfu::evaluate(param::semaphore_submit{
		.semaphore = VkSemaphore{VK_NULL_HANDLE},
		.stage_mask = {.all_commands = 1},
	});
	assert(wait_semaphore.sType == VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
	assert(wait_semaphore.stageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
	assert(signal_semaphore.stageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
	assert(command_buffer.sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);

	auto const submit = ::vkfu::evaluate(param::submit2{
		.wait_semaphore_infos = ::std::span{&wait_semaphore, 1u},
		.command_buffer_infos = ::std::span{&command_buffer, 1u},
		.signal_semaphore_infos = ::std::span{&signal_semaphore, 1u},
	});
	assert(submit.sType == VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
	assert(submit.waitSemaphoreInfoCount == 1);
	assert(submit.commandBufferInfoCount == 1);
	assert(submit.signalSemaphoreInfoCount == 1);
	assert(submit.pCommandBufferInfos == &command_buffer);

	auto const swapchain = VkSwapchainKHR{VK_NULL_HANDLE};
	auto const image_index = ::std::uint32_t{2};
	auto const render_finished = VkSemaphore{VK_NULL_HANDLE};
	auto const present = ::vkfu::evaluate(param::khr::present{
		.wait_semaphores = ::std::span{&render_finished, 1u},
		.swapchain_count = 1,
		.swapchains = &swapchain,
		.image_indices = &image_index,
	});
	assert(present.sType == VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
	assert(present.waitSemaphoreCount == 1);
	assert(present.swapchainCount == 1);
	assert(present.pImageIndices == &image_index);
}

void test_command_pool_expression()
{
	auto const info = ::vkfu::evaluate(param::command_pool{
		.flags = {.transient = 1},
		.queue_family_index = 3,
	});
	assert(info.sType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
	assert(info.flags == VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	assert(info.queueFamilyIndex == 3);

	auto const reset_pool = ::vkfu::evaluate(param::command_pool{
		.flags = {.transient = 1, .reset_command_buffer = 1},
		.queue_family_index = 0,
	});
	assert(reset_pool.flags
		== (VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
}

void test_shader_module_expression()
{
	auto const words = ::std::array<::std::uint32_t, 4>{0x07230203u, 0, 0, 0};
	auto const info = ::vkfu::evaluate(param::shader_module{
		.code_size = words.size() * sizeof(::std::uint32_t),
		.code = words.data(),
	});
	assert(info.sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
	assert(info.codeSize == 16);
	assert(info.pCode == words.data());
}

int main()
{
	test_instance_expression();
	test_debug_messenger_expression();
	test_device_expression();
	test_swapchain_expression();
	test_image_view_expression();
	test_begin_frame_expressions();
	test_secondary_command_buffer_expressions();
	test_submit_present_expressions();
	test_command_pool_expression();
	test_shader_module_expression();
}
