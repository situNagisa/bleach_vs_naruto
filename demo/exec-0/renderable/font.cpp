module;

#include <cassert>
#include <vkfu/vulkan/v1.4.328/version_1_0.h>
#include <vkfu/vulkan/v1.4.328/version_1_1.h>
#include <vkfu/vulkan/v1.4.328/version_1_3.h>
#include <vkkl/vkkl.h>
#include <glm/glm.hpp>
#include <bvn/assets/sprite_clip.h>
#include <ft2build.h>
#include FT_FREETYPE_H

module demo.renderable.font;


struct font::impl
{
	static void check(::VkResult result, char const* operation)
	{
		if (result != VK_SUCCESS)
			throw ::std::runtime_error{ operation };
	}

	

	struct point
	{
		float x, y;
	};
	using color = ::std::array<float, 4>;
	struct vertex_type
	{
		point position, uv;
		color color;
	};

	struct geometry
	{
		::std::vector<vertex_type> vertices{};

		void texture(point lt, point rt, point rb, point lb, point uv_min = { 0.0f, 0.0f }, point uv_max = { 1.0f, 1.0f })
		{
			assert(vertices.size() <= (::std::numeric_limits<::std::uint32_t>::max)() - 6u);
			auto tint = color{ 1.0f, 1.0f, 1.0f, 1.0f };
			vertices.append_range(::std::array{
				vertex_type{ lt, uv_min, tint },
				vertex_type{ rt, {uv_max.x, uv_min.y}, tint },
				vertex_type{ rb, uv_max, tint },
				vertex_type{ lt, uv_min, tint },
				vertex_type{ rb, uv_max, tint },
				vertex_type{ lb, {uv_min.x, uv_max.y}, tint } });
		}

	};

	constexpr static auto vertex_code = ::std::array{
		0x07230203u, 0x00010600u, 0x0008000bu, 0x00000023u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
		0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
		0x000b000fu, 0x00000000u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000du, 0x00000012u, 0x0000001cu,
		0x0000001du, 0x0000001fu, 0x00000021u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u,
		0x6e69616du, 0x00000000u, 0x00060005u, 0x0000000bu, 0x505f6c67u, 0x65567265u, 0x78657472u, 0x00000000u,
		0x00060006u, 0x0000000bu, 0x00000000u, 0x505f6c67u, 0x7469736fu, 0x006e6f69u, 0x00070006u, 0x0000000bu,
		0x00000001u, 0x505f6c67u, 0x746e696fu, 0x657a6953u, 0x00000000u, 0x00070006u, 0x0000000bu, 0x00000002u,
		0x435f6c67u, 0x4470696cu, 0x61747369u, 0x0065636eu, 0x00070006u, 0x0000000bu, 0x00000003u, 0x435f6c67u,
		0x446c6c75u, 0x61747369u, 0x0065636eu, 0x00030005u, 0x0000000du, 0x00000000u, 0x00050005u, 0x00000012u,
		0x69736f70u, 0x6e6f6974u, 0x00000000u, 0x00040005u, 0x0000001cu, 0x67617266u, 0x0076755fu, 0x00030005u,
		0x0000001du, 0x00007675u, 0x00050005u, 0x0000001fu, 0x67617266u, 0x6c6f635fu, 0x0000726fu, 0x00040005u,
		0x00000021u, 0x6f6c6f63u, 0x00000072u, 0x00030047u, 0x0000000bu, 0x00000002u, 0x00050048u, 0x0000000bu,
		0x00000000u, 0x0000000bu, 0x00000000u, 0x00050048u, 0x0000000bu, 0x00000001u, 0x0000000bu, 0x00000001u,
		0x00050048u, 0x0000000bu, 0x00000002u, 0x0000000bu, 0x00000003u, 0x00050048u, 0x0000000bu, 0x00000003u,
		0x0000000bu, 0x00000004u, 0x00040047u, 0x00000012u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x0000001cu,
		0x0000001eu, 0x00000000u, 0x00040047u, 0x0000001du, 0x0000001eu, 0x00000001u, 0x00040047u, 0x0000001fu,
		0x0000001eu, 0x00000001u, 0x00040047u, 0x00000021u, 0x0000001eu, 0x00000002u, 0x00020013u, 0x00000002u,
		0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u,
		0x00000006u, 0x00000004u, 0x00040015u, 0x00000008u, 0x00000020u, 0x00000000u, 0x0004002bu, 0x00000008u,
		0x00000009u, 0x00000001u, 0x0004001cu, 0x0000000au, 0x00000006u, 0x00000009u, 0x0006001eu, 0x0000000bu,
		0x00000007u, 0x00000006u, 0x0000000au, 0x0000000au, 0x00040020u, 0x0000000cu, 0x00000003u, 0x0000000bu,
		0x0004003bu, 0x0000000cu, 0x0000000du, 0x00000003u, 0x00040015u, 0x0000000eu, 0x00000020u, 0x00000001u,
		0x0004002bu, 0x0000000eu, 0x0000000fu, 0x00000000u, 0x00040017u, 0x00000010u, 0x00000006u, 0x00000002u,
		0x00040020u, 0x00000011u, 0x00000001u, 0x00000010u, 0x0004003bu, 0x00000011u, 0x00000012u, 0x00000001u,
		0x0004002bu, 0x00000006u, 0x00000014u, 0x00000000u, 0x0004002bu, 0x00000006u, 0x00000015u, 0x3f800000u,
		0x00040020u, 0x00000019u, 0x00000003u, 0x00000007u, 0x00040020u, 0x0000001bu, 0x00000003u, 0x00000010u,
		0x0004003bu, 0x0000001bu, 0x0000001cu, 0x00000003u, 0x0004003bu, 0x00000011u, 0x0000001du, 0x00000001u,
		0x0004003bu, 0x00000019u, 0x0000001fu, 0x00000003u, 0x00040020u, 0x00000020u, 0x00000001u, 0x00000007u,
		0x0004003bu, 0x00000020u, 0x00000021u, 0x00000001u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u,
		0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du, 0x00000010u, 0x00000013u, 0x00000012u, 0x00050051u,
		0x00000006u, 0x00000016u, 0x00000013u, 0x00000000u, 0x00050051u, 0x00000006u, 0x00000017u, 0x00000013u,
		0x00000001u, 0x00070050u, 0x00000007u, 0x00000018u, 0x00000016u, 0x00000017u, 0x00000014u, 0x00000015u,
		0x00050041u, 0x00000019u, 0x0000001au, 0x0000000du, 0x0000000fu, 0x0003003eu, 0x0000001au, 0x00000018u,
		0x0004003du, 0x00000010u, 0x0000001eu, 0x0000001du, 0x0003003eu, 0x0000001cu, 0x0000001eu, 0x0004003du,
		0x00000007u, 0x00000022u, 0x00000021u, 0x0003003eu, 0x0000001fu, 0x00000022u, 0x000100fdu, 0x00010038u,
	};

	constexpr static auto fragment_code = ::std::array{
		0x07230203u, 0x00010600u, 0x0008000bu, 0x00000018u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
		0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
		0x0009000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x0000000du, 0x00000011u,
		0x00000015u, 0x00030010u, 0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u,
		0x00000004u, 0x6e69616du, 0x00000000u, 0x00060005u, 0x00000009u, 0x7074756fu, 0x635f7475u, 0x726f6c6fu,
		0x00000000u, 0x00060005u, 0x0000000du, 0x67616d69u, 0x65745f65u, 0x72757478u, 0x00000065u, 0x00040005u,
		0x00000011u, 0x67617266u, 0x0076755fu, 0x00050005u, 0x00000015u, 0x67617266u, 0x6c6f635fu, 0x0000726fu,
		0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x0000000du, 0x00000021u, 0x00000000u,
		0x00040047u, 0x0000000du, 0x00000022u, 0x00000000u, 0x00040047u, 0x00000011u, 0x0000001eu, 0x00000000u,
		0x00040047u, 0x00000015u, 0x0000001eu, 0x00000001u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u,
		0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u,
		0x00040020u, 0x00000008u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u,
		0x00090019u, 0x0000000au, 0x00000006u, 0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u,
		0x00000000u, 0x0003001bu, 0x0000000bu, 0x0000000au, 0x00040020u, 0x0000000cu, 0x00000000u, 0x0000000bu,
		0x0004003bu, 0x0000000cu, 0x0000000du, 0x00000000u, 0x00040017u, 0x0000000fu, 0x00000006u, 0x00000002u,
		0x00040020u, 0x00000010u, 0x00000001u, 0x0000000fu, 0x0004003bu, 0x00000010u, 0x00000011u, 0x00000001u,
		0x00040020u, 0x00000014u, 0x00000001u, 0x00000007u, 0x0004003bu, 0x00000014u, 0x00000015u, 0x00000001u,
		0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du,
		0x0000000bu, 0x0000000eu, 0x0000000du, 0x0004003du, 0x0000000fu, 0x00000012u, 0x00000011u, 0x00050057u,
		0x00000007u, 0x00000013u, 0x0000000eu, 0x00000012u, 0x0004003du, 0x00000007u, 0x00000016u, 0x00000015u,
		0x00050085u, 0x00000007u, 0x00000017u, 0x00000013u, 0x00000016u, 0x0003003eu, 0x00000009u, 0x00000017u,
		0x000100fdu, 0x00010038u,
	};

	struct texture_type
	{
		::bvn::assets::sprite_clip_data source;
		::vkkl::device_memory memory{};
		::vkkl::image image{};
		::vkkl::image_view view{};
		::vkkl::sampler sampler{};

		texture_type(texture_type&&) noexcept = default;
		texture_type& operator=(texture_type&&)= delete;

		texture_type(
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
				throw ::std::runtime_error{ "image: cannot upload an empty clip" };

			auto image_properties = VkImageFormatProperties{};
			check(::vkGetPhysicalDeviceImageFormatProperties(physical_device,
				VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, &image_properties),
				"image: texture array format is unsupported");
			if (source.frame_width > image_properties.maxExtent.width
				|| source.frame_height > image_properties.maxExtent.height
				|| source.frame_count > image_properties.maxArrayLayers)
				throw ::std::runtime_error{ "image: clip exceeds texture array limits" };

			auto byte_count = ::std::size_t{ 4 };
			for (auto const dimension : ::std::array{ static_cast<::std::size_t>(source.frame_width), static_cast<::std::size_t>(source.frame_height), source.frame_count })
			{
				if (dimension > (::std::numeric_limits<::std::size_t>::max)() / byte_count)
					throw ::std::runtime_error{ "image: clip byte size overflows" };
				byte_count *= dimension;
			}
			if (source.frames_rgba8.size() != byte_count)
				throw ::std::runtime_error{ "image: pixel storage does not match frame metadata" };

			auto const layer_count = static_cast<::std::uint32_t>(source.frame_count);
			auto const extent = VkExtent3D{ source.frame_width, source.frame_height, 1 };
			auto const subresources = param::image_subresource_range{
				.aspect_mask = {.color = 1}, .level_count = 1, .layer_count = layer_count
			};
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
				throw ::std::runtime_error{ "image: no compatible image memory type" };
			memory = ::vkkl::device_memory{ device, ::vkfu::allocate_memory(device, param::memory{
				.allocation_size = requirements.size, .type_index = *memory_type
			}) };
			::vkfu::bind_image_memory2(device, ::std::array{ ::vkfu::evaluate(param::bind_image_memory{ .image = image.handle, .memory = memory.handle }) });
			view = ::vkkl::image_view{ device, ::vkfu::create_image_view(device, param::image_view{
				.image = image.handle,
				.view_type = image_view_type::dim_2d,
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

			auto staging = buffer_type{ device, physical_device, source.frames_rgba8, VK_BUFFER_USAGE_TRANSFER_SRC_BIT };
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
				.src_buffer = staging.handle(),
				.dst_image = image.handle,
				.dst_image_layout = image_layout::transfer_dst_optimal,
				.regions = ::std::array{ ::vkfu::evaluate(param::buffer_image_copy2{
					.image_subresource = param::image_subresource_layers{ .aspect_mask = {.color = 1}, .layer_count = layer_count },
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
			check(::vkEndCommandBuffer(command.handle), "image: end upload");
			auto fence = ::vkkl::fence{ device, ::vkfu::create_fence(device, param::fence{}) };
			::vkfu::queue_submit(graphics_queue, ::std::array{ ::vkfu::evaluate(param::submit{ .command_buffers = ::std::array{ command.handle } }) }, fence.handle);
			::vkfu::wait_for_fences(device, ::std::array{ fence.handle }, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());
		}
	};

	struct texture_graphic_pipeline_type
	{
		::vkkl::descriptor_set_layout descriptor_layout;
		::vkkl::pipeline_layout layout;
		::vkkl::pipeline pipeline;
		::vkkl::descriptor_pool descriptor_pool;
		::std::array<::vkkl::descriptor_set, 1> descriptors;

		texture_graphic_pipeline_type(VkDevice device, VkFormat format)
			: descriptor_layout(device, ::vkfu::create_descriptor_set_layout(device, ::vkfu::param::descriptor_set_layout{
					.bindings = ::std::array{ ::vkfu::evaluate(::vkfu::param::descriptor_set_layout_binding{
					.descriptor_type = ::vkfu::enums::descriptor_type::combined_image_sampler,
					.descriptor_count = 1,
					.stage_flags = {.fragment = 1},
				})}}))
			, layout(device, ::vkfu::create_pipeline_layout(device, ::vkfu::param::pipeline_layout{
					.set_layouts = ::std::span{ &descriptor_layout.handle, 1u }
				}))
			, pipeline([&]
				{
					namespace param = ::vkfu::param;
					using namespace ::vkfu::enums;

					auto shader = [&](::std::span<::std::uint32_t const> code)
						{
							return ::vkkl::shader_module{ device, ::vkfu::create_shader_module(device, param::shader_module{.code_size = code.size_bytes(), .code = code.data()}) };
						};
					auto vertex = shader(vertex_code);
					auto fragment = shader(fragment_code);
					auto stages = ::std::array{
						::vkfu::evaluate(param::state::shader_stage{.stage = shader_stage::vertex, .module = vertex.handle, .name = "main"}),
						::vkfu::evaluate(param::state::shader_stage{.stage = shader_stage::fragment, .module = fragment.handle, .name = "main"})
					};
					auto attachment = ::vkfu::evaluate(param::state::color_blend_attachment{
						.blend_enable = true,
						.src_color_blend_factor = blend_factor::src_alpha,
						.dst_color_blend_factor = blend_factor::one_minus_src_alpha,
						.src_alpha_blend_factor = blend_factor::one,
						.dst_alpha_blend_factor = blend_factor::one_minus_src_alpha,
						.color_write_mask = {.r = 1, .g = 1, .b = 1, .a = 1}
						});
					return ::vkkl::pipeline{ device,
						::vkfu::create_graphics_pipeline(device, VK_NULL_HANDLE, param::graphics_pipeline{
							.stage_count = static_cast<::std::uint32_t>(stages.size()),
							.stages = stages.data(),
							.vertex_input_state = param::state::vertex_input{
								.vertex_binding_descriptions = ::std::array{ ::vkfu::evaluate(param::vertex_input_binding_description{.stride = sizeof(vertex_type), .input_rate = vertex_input_rate::vertex }) },
								.vertex_attribute_descriptions = ::std::array{
									::vkfu::evaluate(param::vertex_input_attribute_description{.location = 0, .format = format::r32g32_sfloat, .offset = offsetof(vertex_type, position)}),
									::vkfu::evaluate(param::vertex_input_attribute_description{.location = 1, .format = format::r32g32_sfloat, .offset = offsetof(vertex_type, uv)}),
									::vkfu::evaluate(param::vertex_input_attribute_description{.location = 2, .format = format::r32g32b32a32_sfloat, .offset = offsetof(vertex_type, color)})
								}
							},
							.input_assembly_state = param::state::input_assembly{.topology = primitive_topology::triangle_list},
							.viewport_state = param::state::viewport{.viewport_count = 1, .scissor_count = 1},
							.rasterization_state = param::state::rasterization{
								.polygon_mode = polygon_mode::fill,
								.front_face = front_face::counter_clockwise,
								.line_width = 1.0f
							},
							.multisample_state = param::state::multisample{.rasterization_samples = sample_count::count_1},
							.color_blend_state = param::state::color_blend{.attachment_count = 1, .attachments = &attachment},
							.dynamic_state = param::state::dynamic{.states = ::std::array{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }},
							.layout = layout.handle,
						} | param::option::pipeline_rendering{.color_attachment_formats = ::std::array{ format } }
						) };
				}())
			, descriptor_pool(device, ::vkfu::create_descriptor_pool(device, ::vkfu::param::descriptor_pool{
					.flags = {.free_descriptor_set = 1},
					.max_sets = 2,
					.pool_sizes = ::std::array{ ::vkfu::evaluate(::vkfu::param::descriptor_pool_size{
						.type = ::vkfu::enums::descriptor_type::combined_image_sampler,
						.descriptor_count = 2
					})} }))
			, descriptors([&]
				{
					auto descriptors = ::std::array{
						VkDescriptorSet{ VK_NULL_HANDLE },
					};
					::vkfu::allocate_descriptor_sets(
						device
						, ::vkfu::param::descriptor_set{ .descriptor_pool = descriptor_pool.handle, .set_layouts = ::std::array{ descriptor_layout.handle } }
						, descriptors
					);
					return ::std::array{
						::vkkl::descriptor_set{ device, descriptor_pool.handle, descriptors[0] },
					};
				}())
		{}

		void update_descriptor_sets(VkDevice device, VkDescriptorSet descriptor_set, VkSampler sampler, VkImageView view)
		{
			auto const image_info = ::vkfu::evaluate(::vkfu::param::descriptor_image{
				.sampler = sampler,
				.image_view = view,
				.image_layout = ::vkfu::enums::image_layout::shader_read_only_optimal }
			);
			::vkfu::update_descriptor_sets(
				device
				, ::std::array{ ::vkfu::evaluate(::vkfu::param::write_descriptor_set{
					.dst_set = descriptor_set,
					.descriptor_count = 1,
					.descriptor_type = ::vkfu::enums::descriptor_type::combined_image_sampler,
					.image_info = &image_info }
					) }
				, {}
			);
		}
	};

	struct text
	{
		texture_type texture;
		texture_graphic_pipeline_type pipeline;

		text(
			::bvn::assets::image_rgba8 img
			, VkDevice device
			, VkPhysicalDevice physical_device
			, VkQueue graphics_queue
			, ::std::uint32_t graphics_queue_family
			, VkFormat format
		)
			: texture(::bvn::assets::sprite_clip_data{ img.width, img.height, 1, ::std::move(img.pixels) }, device, physical_device, graphics_queue, graphics_queue_family)
			, pipeline(device, format)
		{
			auto const image_info = ::vkfu::evaluate(::vkfu::param::descriptor_image{
				.sampler = texture.sampler.handle,
				.image_view = texture.view.handle,
				.image_layout = ::vkfu::enums::image_layout::shader_read_only_optimal }
				);
			::vkfu::update_descriptor_sets(
				device
				, ::std::array{ ::vkfu::evaluate(::vkfu::param::write_descriptor_set{
					.dst_set = pipeline.descriptors[0].handle,
					.descriptor_count = 1,
					.descriptor_type = ::vkfu::enums::descriptor_type::combined_image_sampler,
					.image_info = &image_info }
					) }
				, {}
			);
		}

		[[nodiscard]] buffer_type draw( VkDevice device , VkPhysicalDevice physical_device , VkCommandBuffer command_buffer , ::glm::mat3x3 transform, ::glm::vec4 tint ) const
		{
			auto const extent = point{
				static_cast<float>(texture.source.frame_width),
				static_cast<float>(texture.source.frame_height)
			};

			auto transform_point = [&](point p)
				{
					auto const result = transform * ::glm::vec3{ p.x, p.y, 1.0f };
					return point{ result.x, result.y };
				};

			auto const p0 = transform_point({ 0.0f, 0.0f });
			auto const p1 = transform_point({ extent.x, 0.0f });
			auto const p2 = transform_point({ extent.x, extent.y });
			auto const p3 = transform_point({ 0.0f, extent.y });

			auto g = geometry{};
			g.texture(p0, p1, p2, p3, { 0.0f, 0.0f }, { 1.0f, 1.0f });
			for (auto& vertex : g.vertices)
				vertex.color = { tint.r, tint.g, tint.b, tint.a };

			::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
			auto result = buffer_type{ device, physical_device, ::std::as_bytes(::std::span{ g.vertices }), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
			::vkfu::cmd_bind_descriptor_sets(command_buffer, ::vkfu::enums::pipeline_bind_point::graphics, pipeline.layout.handle, 0, ::std::array{ pipeline.descriptors[0].handle }, {});
			::vkfu::cmd_bind_vertex_buffers(command_buffer, 0, ::std::array{ result.handle() }, ::std::array{ ::VkDeviceSize{} });
			::vkCmdDraw(command_buffer, static_cast<::std::uint32_t>(g.vertices.size()), 1, 0, 0);
			return result;
		}
	};

	struct library_deleter
	{
		void operator()(FT_Library value) const noexcept { ::FT_Done_FreeType(value); }
	};
	struct face_deleter
	{
		void operator()(FT_Face value) const noexcept { ::FT_Done_Face(value); }
	};

	::std::vector<unsigned char> file_bytes;
	::std::unique_ptr<::std::remove_pointer_t<FT_Library>, library_deleter> library;
	::std::unique_ptr<::std::remove_pointer_t<FT_Face>, face_deleter> face;

	impl(impl&&) noexcept = default;
	impl& operator=(impl&&)= delete;

	explicit impl(::std::filesystem::path const& path)
	{
		auto file = ::std::ifstream{ path, ::std::ios::binary | ::std::ios::ate };
		if (!file)
			throw ::std::runtime_error{ "font: cannot open font file" };
		auto const length = file.tellg();
		if (length <= 0 || length > (::std::numeric_limits<FT_Long>::max)())
			throw ::std::runtime_error{ "font: invalid font file size" };
		file_bytes.resize(static_cast<::std::size_t>(length));
		file.seekg(0);
		if (!file.read(reinterpret_cast<char*>(file_bytes.data()), static_cast<::std::streamsize>(file_bytes.size())))
			throw ::std::runtime_error{ "font: cannot read font file" };
		auto raw_library = FT_Library{};
		if (::FT_Init_FreeType(&raw_library))
			throw ::std::runtime_error{ "font: FreeType initialization failed" };
		library.reset(raw_library);
		auto raw_face = FT_Face{};
		if (::FT_New_Memory_Face(library.get(), file_bytes.data(), static_cast<FT_Long>(file_bytes.size()), 0, &raw_face))
			throw ::std::runtime_error{ "font: unsupported or invalid font file" };
		face.reset(raw_face);
		if (::FT_Select_Charmap(face.get(), FT_ENCODING_UNICODE))
			throw ::std::runtime_error{ "font: Unicode character map unavailable" };
	}

	[[nodiscard]] static ::std::u32string decode_utf8(::std::string_view input)
	{
		auto result = ::std::u32string{};
		for (auto offset = ::std::size_t{}; offset < input.size();)
		{
			auto const first = static_cast<unsigned char>(input[offset++]);
			auto codepoint = ::std::uint32_t{ first };
			auto remaining = 0;
			auto minimum = ::std::uint32_t{};
			if (first < 0x80) {}
			else if (first >= 0xc2 && first <= 0xdf) { codepoint &= 0x1f; remaining = 1; minimum = 0x80; }
			else if (first >= 0xe0 && first <= 0xef) { codepoint &= 0x0f; remaining = 2; minimum = 0x800; }
			else if (first >= 0xf0 && first <= 0xf4) { codepoint &= 0x07; remaining = 3; minimum = 0x10000; }
			else { throw ::std::runtime_error{ "font: invalid UTF-8" }; }
			while (remaining-- > 0)
			{
				if (offset == input.size())
					throw ::std::runtime_error{ "font: truncated UTF-8" };
				auto const next = static_cast<unsigned char>(input[offset++]);
				if ((next & 0xc0) != 0x80)
					throw ::std::runtime_error{ "font: invalid UTF-8 continuation" };
				codepoint = (codepoint << 6) | (next & 0x3f);
			}
			if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
				throw ::std::runtime_error{ "font: invalid Unicode codepoint" };
			result.push_back(static_cast<char32_t>(codepoint));
		}
		return result;
	}

	[[nodiscard]] text prepare( ::std::string_view utf8, ::std::uint32_t pixel_height, ::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format )
	{
		assert(pixel_height > 0);
		auto const codepoints = decode_utf8(utf8);
		if (::FT_Set_Pixel_Sizes(face.get(), 0, pixel_height))
			throw ::std::runtime_error{ "font: cannot set pixel size" };
		struct glyph_bitmap
		{
			::std::int64_t x{}, y{};
			unsigned int width{}, height{};
			::std::vector<unsigned char> coverage;
		};
		auto glyphs = ::std::vector<glyph_bitmap>{};
		auto pen_x = ::std::int64_t{};
		auto baseline = static_cast<::std::int64_t>(face->size->metrics.ascender / 64);
		auto const line_height = (::std::max)(::std::int64_t{ 1 }, static_cast<::std::int64_t>(face->size->metrics.height / 64));
		auto min_x = ::std::int64_t{};
		auto min_y = ::std::int64_t{};
		auto max_x = ::std::int64_t{ 1 };
		auto max_y = line_height;
		auto previous = FT_UInt{};
		auto properties = ::VkPhysicalDeviceProperties{};
		::vkGetPhysicalDeviceProperties(physical_device, &properties);
		auto const limit = static_cast<::std::int64_t>(properties.limits.maxImageDimension2D);
		for (auto codepoint : codepoints)
		{
			if (codepoint == U'\n')
			{
				pen_x = 0;
				baseline += line_height;
				max_y += line_height;
				previous = 0;
				if (max_y - min_y > limit)
					throw ::std::runtime_error{ "font: text exceeds image limits" };
				continue;
			}
			auto const index = ::FT_Get_Char_Index(face.get(), static_cast<FT_ULong>(codepoint));
			if (!index)
				throw ::std::runtime_error{ "font: character is missing from font" };
			if (previous && FT_HAS_KERNING(face.get()))
			{
				auto kerning = FT_Vector{};
				if (::FT_Get_Kerning(face.get(), previous, index, FT_KERNING_DEFAULT, &kerning))
					throw ::std::runtime_error{ "font: cannot read kerning" };
				pen_x += kerning.x / 64;
			}
			if (::FT_Load_Glyph(face.get(), index, FT_LOAD_DEFAULT) || ::FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
				throw ::std::runtime_error{ "font: cannot rasterize glyph" };
			auto const& bitmap = face->glyph->bitmap;
			auto glyph = glyph_bitmap{
				.x = pen_x + face->glyph->bitmap_left,
				.y = baseline - face->glyph->bitmap_top,
				.width = bitmap.width, .height = bitmap.rows, .coverage = {}
			};
			min_x = (::std::min)(min_x, glyph.x);
			min_y = (::std::min)(min_y, glyph.y);
			pen_x += face->glyph->advance.x / 64;
			max_x = (::std::max)({ max_x, pen_x, glyph.x + glyph.width });
			max_y = (::std::max)(max_y, glyph.y + glyph.height);
			if (max_x - min_x > limit || max_y - min_y > limit)
				throw ::std::runtime_error{ "font: text exceeds image limits" };
			if (bitmap.width && bitmap.rows)
			{
				if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.num_grays != 256)
					throw ::std::runtime_error{ "font: unsupported glyph bitmap format" };
				glyph.coverage.resize(static_cast<::std::size_t>(bitmap.width) * bitmap.rows);
				for (auto row = 0u; row < bitmap.rows; ++row)
				{
					auto const source_row = bitmap.pitch >= 0 ? row : bitmap.rows - row - 1;
					auto const pitch = ::std::abs(static_cast<::std::int64_t>(bitmap.pitch));
					::std::memcpy(glyph.coverage.data() + static_cast<::std::size_t>(row) * bitmap.width,
						bitmap.buffer + source_row * pitch, bitmap.width);
				}
			}
			glyphs.push_back(::std::move(glyph));
			previous = index;
		}
		auto pixels = ::bvn::assets::image_rgba8{
			.width = static_cast<::std::uint32_t>(max_x - min_x),
			.height = static_cast<::std::uint32_t>(max_y - min_y), .pixels = {}
		};
		if (pixels.height > (::std::numeric_limits<::std::size_t>::max)() / 4 / pixels.width)
			throw ::std::runtime_error{ "font: text byte size overflows" };
		pixels.pixels.resize(static_cast<::std::size_t>(pixels.width) * pixels.height * 4);
		for (auto const& glyph : glyphs)
			for (auto row = 0u; row < glyph.height; ++row)
				for (auto column = 0u; column < glyph.width; ++column)
				{
					auto const offset = (static_cast<::std::size_t>(glyph.y - min_y + row) * pixels.width + static_cast<::std::size_t>(glyph.x - min_x + column)) * 4;
					auto const coverage = glyph.coverage[static_cast<::std::size_t>(row) * glyph.width + column];
					auto const old_alpha = ::std::to_integer<unsigned int>(pixels.pixels[offset + 3]);
					pixels.pixels[offset] = pixels.pixels[offset + 1] = pixels.pixels[offset + 2] = ::std::byte{ 255 };
					pixels.pixels[offset + 3] = static_cast<::std::byte>(coverage + (old_alpha * (255 - coverage) + 127) / 255);
				}
		return text{ ::std::move(pixels), device, physical_device, graphics_queue, graphics_queue_family, format };
	}
};

font::~font() = default;
font::font(font&&) noexcept = default;
font& font::operator=(font&&) noexcept= default;

font::font(::std::filesystem::path const& path) : _impl(::std::make_unique<impl>(path)) {}

struct font::text::impl
{
	font::impl::text value;
};

font::text::text() = default;
font::text::~text() = default;
font::text::text(text&&) noexcept = default;
font::text& font::text::operator=(text&&) noexcept= default;

font::text font::prepare(::std::string_view utf8, ::std::uint32_t pixel_height, ::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format)
{
	auto result = text{};
	result._impl = ::std::make_unique<text::impl>(text::impl{ _impl->prepare(utf8, pixel_height, device, physical_device, graphics_queue, graphics_queue_family, format) });
	return result;
}

::std::u32string font::decode_utf8(::std::string_view input)
{
	return impl::decode_utf8(input);
}

::VkExtent2D font::text::extent() const noexcept
{
	return { _impl->value.texture.source.frame_width, _impl->value.texture.source.frame_height };
}

buffer_type font::text::draw(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkCommandBuffer command_buffer, ::glm::mat3x3 transform, ::glm::vec4 tint) const
{
	return _impl->value.draw(device, physical_device, command_buffer, transform, tint);
}
