#pragma once

#include <cassert>
#include <ranges>

#include <vkkl/vkkl.h>
#include <vkfu/generated/vulkan-v1.4.328.h>

#include <bvn/assets/assets.h>

#include "../detail/demo_vulkan.h"

#include "./buffer_type.h"

struct texture_type
{
	constexpr static auto subresources = ::vkfu::evaluate(::vkfu::param::image_subresource_range{ .aspect_mask = {.color = 1}, .level_count = 1, .layer_count = 1 });

	::bvn::assets::image_rgba8 source;
	::vkkl::image image;
	::vkkl::device_memory memory;
	::vkkl::image_view view;
	buffer_type staging;
	::vkkl::sampler sampler;

	texture_type(
		::bvn::assets::image_rgba8 img
		, VkDevice device
		, VkPhysicalDevice physical_device
		, VkQueue graphics_queue
		, ::std::uint32_t graphics_queue_family
	)
		: source(::std::move(img))
		, image(device, ::vkfu::create_image(device, ::vkfu::param::image{
					.type = ::vkfu::enums::image_type::dim_2d,
					.format = ::vkfu::enums::format::r8g8b8a8_srgb,
					.extent = ::VkExtent3D{ source.width, source.height, 1 },
					.mip_levels = 1,
					.array_layers = 1,
					.samples = ::vkfu::enums::sample_count::count_1,
					.usage = {.transfer_dst = 1, .sampled = 1}
			}))
		, memory([&]
			{
				auto req = ::vkfu::get_image_memory_requirements2(device, ::vkfu::param::image_memory_requirements2{ .image = this->image.handle }).head().memoryRequirements;
				auto properties = ::vkfu::get_physical_device_memory_properties2(physical_device).head().memoryProperties;
				auto flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
				auto index = ::std::optional<::std::uint32_t>{};
				for (auto&& [i, type] : properties.memoryTypes | ::std::views::take(properties.memoryTypeCount) | ::std::views::enumerate)
				{
					if (req.memoryTypeBits & (1u << i) && (type.propertyFlags & flags) == flags)
					{
						index.emplace(i);
						break;
					}
				}
				if (!index)
					throw ::std::runtime_error{ "main menu: no compatible Vulkan memory type" };
				return ::vkkl::device_memory(device, ::vkfu::allocate_memory(device, ::vkfu::param::memory{ .allocation_size = req.size, .type_index = *index }));
			}())
		, view(device, VK_NULL_HANDLE)
		, staging(device, physical_device, source.pixels, ::vkfu::param::buffer::usage_type{ .transfer_src = 1 })
		, sampler(device, ::vkfu::create_sampler(device, ::vkfu::param::sampler{
				.mag_filter = ::vkfu::enums::filter::linear,
				.min_filter = ::vkfu::enums::filter::linear,
				.address_mode_u = ::vkfu::enums::sampler_address_mode::clamp_to_edge,
				.address_mode_v = ::vkfu::enums::sampler_address_mode::clamp_to_edge,
				.address_mode_w = ::vkfu::enums::sampler_address_mode::clamp_to_edge
			}))
	{
		namespace param = ::vkfu::param;
		using namespace ::vkfu::enums;

		::vkfu::bind_image_memory2(device, ::std::array{ ::vkfu::evaluate(::vkfu::param::bind_image_memory{.image = image.handle, .memory = memory.handle }) });
		view.handle = ::vkfu::create_image_view(device, ::vkfu::param::image_view{
				.image = this->image.handle,
				.view_type = ::vkfu::enums::image_view_type::dim_2d,
				.format = ::vkfu::enums::format::r8g8b8a8_srgb,
				.subresource_range = subresources
			});

		auto pool = ::vkkl::command_pool(device, ::vkfu::create_command_pool(device, ::vkfu::param::command_pool{ .flags = {.transient = 1}, .queue_family_index = graphics_queue_family, }));
		auto command = ::vkkl::command_buffer{ device, pool.handle, VK_NULL_HANDLE };
		::vkfu::allocate_command_buffers(device, ::vkfu::param::command_buffer{ .command_pool = pool.handle, .level = command_buffer_level::primary, .command_buffer_count = 1 }, ::std::span{ &command.handle, 1u });
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
			.regions = ::std::array{ ::vkfu::evaluate(param::buffer_image_copy2{.image_subresource = ::vkfu::evaluate(param::image_subresource_layers{.aspect_mask = {.color = 1}, .layer_count = 1}), .image_extent = ::VkExtent3D{ source.width, source.height, 1 } }) }
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
			.subresource_range = subresources }
			) } });
		::consumer_arch_vulkan::check(::vkEndCommandBuffer(command.handle), "main menu: end upload");
		auto fence = ::vkkl::fence{ device, ::vkfu::create_fence(device, param::fence{}) };
		::vkfu::queue_submit(graphics_queue, ::std::array{ ::vkfu::evaluate(param::submit{.command_buffers = ::std::array{ command.handle } }) }, fence.handle);
		::vkfu::wait_for_fences(device, ::std::array{ fence.handle }, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());
	}
};