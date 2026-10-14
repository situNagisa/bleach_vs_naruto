#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

#include <vkfu/vulkan/v1.4.328.h>

#include "../detail/demo_vulkan.h"

#include "./buffer_type.h"
#include "./sprite_texture.h"

sprite_texture_type::sprite_texture_type(
	::bvn::assets::sprite_clip_data clip
	, VkDevice device
	, VkPhysicalDevice physical_device
	, VkQueue graphics_queue
	, ::std::uint32_t graphics_queue_family
)
	: source(::std::move(clip))
{
	namespace param = ::vkfu::param;
	using namespace ::vkfu::enums;

	if (source.frame_width == 0 || source.frame_height == 0 || source.frame_count == 0)
		throw ::std::runtime_error{ "sprite: cannot upload an empty clip" };

	auto image_properties = VkImageFormatProperties{};
	::consumer_arch_vulkan::check(::vkGetPhysicalDeviceImageFormatProperties(physical_device,
		VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, &image_properties),
		"sprite: texture array format is unsupported");
	if (source.frame_width > image_properties.maxExtent.width
		|| source.frame_height > image_properties.maxExtent.height
		|| source.frame_count > image_properties.maxArrayLayers)
		throw ::std::runtime_error{ "sprite: clip exceeds texture array limits" };

	auto byte_count = ::std::size_t{ 4 };
	for (auto const dimension : ::std::array{ static_cast<::std::size_t>(source.frame_width), static_cast<::std::size_t>(source.frame_height), source.frame_count })
	{
		if (dimension > (::std::numeric_limits<::std::size_t>::max)() / byte_count)
			throw ::std::runtime_error{ "sprite: clip byte size overflows" };
		byte_count *= dimension;
	}
	if (source.frames_rgba8.size() != byte_count)
		throw ::std::runtime_error{ "sprite: pixel storage does not match frame metadata" };

	auto const layer_count = static_cast<::std::uint32_t>(source.frame_count);
	auto const extent = VkExtent3D{ source.frame_width, source.frame_height, 1 };
	auto const subresources = ::vkfu::evaluate(param::image_subresource_range{
		.aspect_mask = {.color = 1}, .level_count = 1, .layer_count = layer_count
	});
	image = ::vkkl::image{ device, ::vkfu::create_image(device, param::image{
		.type = image_type::dim_2d,
		.format = format::r8g8b8a8_srgb,
		.extent = extent,
		.mip_levels = 1,
		.array_layers = layer_count,
		.samples = sample_count::count_1,
		.usage = {.transfer_dst = 1, .sampled = 1}
	}) };

	auto const requirements = ::vkfu::get_image_memory_requirements2(device, param::image_memory_requirements2{ .image = image.handle }).head().memoryRequirements;
	auto const memory_properties = ::vkfu::get_physical_device_memory_properties2(physical_device).head().memoryProperties;
	auto memory_type = ::std::optional<::std::uint32_t>{};
	for (auto&& [index, type] : memory_properties.memoryTypes | ::std::views::take(memory_properties.memoryTypeCount) | ::std::views::enumerate)
	{
		if ((requirements.memoryTypeBits & (1u << index)) && (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			memory_type.emplace(static_cast<::std::uint32_t>(index));
			break;
		}
	}
	if (!memory_type)
		throw ::std::runtime_error{ "sprite: no compatible image memory type" };
	memory = ::vkkl::device_memory{ device, ::vkfu::allocate_memory(device, param::memory{
		.allocation_size = requirements.size, .type_index = *memory_type
	}) };
	::vkfu::bind_image_memory2(device, ::std::array{ ::vkfu::evaluate(param::bind_image_memory{ .image = image.handle, .memory = memory.handle }) });
	view = ::vkkl::image_view{ device, ::vkfu::create_image_view(device, param::image_view{
		.image = image.handle,
		.view_type = image_view_type::dim_2d_array,
		.format = format::r8g8b8a8_srgb,
		.subresource_range = subresources
	}) };
	sampler = ::vkkl::sampler{ device, ::vkfu::create_sampler(device, param::sampler{
		.mag_filter = filter::linear,
		.min_filter = filter::linear,
		.address_mode_u = sampler_address_mode::clamp_to_edge,
		.address_mode_v = sampler_address_mode::clamp_to_edge,
		.address_mode_w = sampler_address_mode::clamp_to_edge
	}) };

	auto staging = buffer_type{ device, physical_device, source.frames_rgba8, ::vkfu::flags::buffer_usage{ .transfer_src = 1 } };
	auto pool = ::vkkl::command_pool{ device, ::vkfu::create_command_pool(device, param::command_pool{
		.flags = {.transient = 1}, .queue_family_index = graphics_queue_family
	}) };
	auto command = ::vkkl::command_buffer{ device, pool.handle, VK_NULL_HANDLE };
	::vkfu::allocate_command_buffers(device, param::command_buffer{
		.command_pool = pool.handle, .level = command_buffer_level::primary, .command_buffer_count = 1
	}, ::std::span{ &command.handle, 1u });
	::vkfu::begin_command_buffer(command.handle, param::command_buffer_begin{ .flags = {.one_time_submit = 1} });
	::vkfu::cmd_pipeline_barrier2(command.handle, param::dependency{ .image_memory_barriers = ::std::array{ ::vkfu::evaluate(param::image_memory_barrier2{
		.src_stage_mask = {.top_of_pipe = 1},
		.dst_stage_mask = {.all_transfer = 1},
		.dst_access_mask = {.transfer_write = 1},
		.old_layout = image_layout::undefined,
		.new_layout = image_layout::transfer_dst_optimal,
		.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
		.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
		.image = image.handle,
		.subresource_range = subresources
	}) } });
	::vkfu::cmd_copy_buffer_to_image2(command.handle, param::copy_buffer_to_image2{
		.src_buffer = staging.handle.handle,
		.dst_image = image.handle,
		.dst_image_layout = image_layout::transfer_dst_optimal,
		.regions = ::std::array{ ::vkfu::evaluate(param::buffer_image_copy2{
			.image_subresource = ::vkfu::evaluate(param::image_subresource_layers{ .aspect_mask = {.color = 1}, .layer_count = layer_count }),
			.image_extent = extent
		}) }
	});
	::vkfu::cmd_pipeline_barrier2(command.handle, param::dependency{ .image_memory_barriers = ::std::array{ ::vkfu::evaluate(param::image_memory_barrier2{
		.src_stage_mask = {.all_transfer = 1},
		.src_access_mask = {.transfer_write = 1},
		.dst_stage_mask = {.fragment_shader = 1},
		.dst_access_mask = {.shader_read = 1},
		.old_layout = image_layout::transfer_dst_optimal,
		.new_layout = image_layout::shader_read_only_optimal,
		.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
		.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
		.image = image.handle,
		.subresource_range = subresources
	}) } });
	::consumer_arch_vulkan::check(::vkEndCommandBuffer(command.handle), "sprite: end upload");
	auto fence = ::vkkl::fence{ device, ::vkfu::create_fence(device, param::fence{}) };
	::vkfu::queue_submit(graphics_queue, ::std::array{ ::vkfu::evaluate(param::submit{ .command_buffers = ::std::array{ command.handle } }) }, fence.handle);
	::vkfu::wait_for_fences(device, ::std::array{ fence.handle }, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());
}
