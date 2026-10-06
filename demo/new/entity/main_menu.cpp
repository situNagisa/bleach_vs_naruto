#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>
#include <vkkl/vkkl.h>
#include <vkfu/generated/vulkan-v1.4.328.h>

#include <bvn/assets/sprite_clip.h>

#include "./main_menu.h"
#include "./input.h"
#include "./render.h"
#include "render.h"
#include "./detail/immovable.h"

struct main_menu::implementation : immovable
{
	auto _update(input::state const& current, input::delta const& changes) -> delta
	{
		auto result = delta{};
		auto const now = ::std::chrono::steady_clock::now();
		auto const up = changes.keyboard.key(input::key_code::up).pressed;
		auto const down = changes.keyboard.key(input::key_code::down).pressed;
		auto const held = static_cast<int>(current.keyboard.key(input::key_code::down))
			- static_cast<int>(current.keyboard.key(input::key_code::up));
		auto direction = int{};
		if (up != down)
		{
			direction = down ? 1 : -1;
			_next_repeat = now + ::std::chrono::milliseconds{350};
		}
		else if (held != _held_direction)
			_next_repeat = now + ::std::chrono::milliseconds{350};
		else if (held != 0 && now >= _next_repeat)
		{
			direction = held;
			_next_repeat = now + ::std::chrono::milliseconds{120};
		}
		_held_direction = held;
		if (direction != 0)
		{
			auto const previous = _state.selected;
			_state.selected = direction < 0 ? (_state.selected + _options.size() - 1) % _options.size()
				: (_state.selected + 1) % _options.size();
			result.moved = previous != _state.selected;
			_state.activated.reset();
		}
		if (changes.keyboard.key(input::key_code::enter).pressed
			|| changes.keyboard.key(input::key_code::keypad_enter).pressed
			|| changes.keyboard.key(input::key_code::space).pressed)
		{
			_state.activated = _state.selected;
			result.activated = _state.selected;
		}
		return result;
	}

	struct _point { float x, y; };
	using _color = ::std::array<float, 4>;
	struct _vertex { _point position, uv; _color color; };

	static auto _allocate_command(::VkDevice device, ::VkCommandPool pool, ::vkfu::enums::command_buffer_level level)
		-> ::vkkl::command_buffer
	{
		auto raw = ::VkCommandBuffer{};
		::vkfu::allocate_command_buffers(
			device,
			::vkfu::param::command_buffer{
				.command_pool = pool, 
				.level = level, 
				.command_buffer_count = 1
			}, 
			::std::span{&raw, 1u});
		return {device, pool, raw};
	}

	struct _glyph
	{
		int x, y, width, height, x_offset, y_offset, advance;
	};

	struct _bitmap_font
	{
		::std::array<::std::optional<_glyph>, 256> glyphs{};
		int width = 0, height = 0, size = 0;

		static auto _field(::std::string_view line, ::std::string_view name) -> int
		{
			auto const offset = line.find(name);
			if (offset == ::std::string_view::npos)
				throw ::std::runtime_error{"main menu: missing BMFont field " + ::std::string{name}};
			auto value = int{};
			auto const result = ::std::from_chars(line.data() + offset + name.size(), line.data() + line.size(), value);
			if (result.ec != ::std::errc{})
				throw ::std::runtime_error{"main menu: invalid BMFont field " + ::std::string{name}};
			return value;
		}

		explicit _bitmap_font(::std::filesystem::path const& path)
		{
			auto file = ::std::ifstream{path};
			if (!file)
				throw ::std::runtime_error{"main menu: cannot open font " + path.string()};
			for (auto line = ::std::string{}; ::std::getline(file, line);)
			{
				if (line.starts_with("info "))
					size = _field(line, " size=");
				else if (line.starts_with("common "))
				{
					width = _field(line, " scaleW=");
					height = _field(line, " scaleH=");
					if (_field(line, " pages=") != 1 || _field(line, " packed=") != 0)
						throw ::std::runtime_error{"main menu: font must use one unpacked image page"};
				}
				else if (line.starts_with("char "))
				{
					auto const id = _field(line, " id=");
					if (id < 0 || id >= static_cast<int>(glyphs.size()) || _field(line, " page=") != 0)
						throw ::std::runtime_error{"main menu: unsupported font character or image page"};
					glyphs[id] = _glyph{_field(line, " x="), _field(line, " y="), _field(line, " width="),
						_field(line, " height="), _field(line, " xoffset="), _field(line, " yoffset="), _field(line, " xadvance=")};
				}
			}
			if (!file.eof() || width <= 0 || height <= 0 || size <= 0)
				throw ::std::runtime_error{"main menu: invalid BMFont data"};
			for (auto const& glyph : glyphs)
				if (glyph && (glyph->x < 0 || glyph->y < 0 || glyph->width < 0 || glyph->height < 0
					|| glyph->x > width - glyph->width || glyph->y > height - glyph->height || glyph->advance < 0))
					throw ::std::runtime_error{"main menu: font glyph exceeds image bounds"};
		}

		[[nodiscard]] auto at(char character) const -> _glyph const&
		{
			auto const& glyph = glyphs[static_cast<unsigned char>(character)];
			if (!glyph)
				throw ::std::runtime_error{"main menu: character is missing from font image"};
			return *glyph;
		}
	};

	struct _geometry
	{
		_point screen;
		_bitmap_font const& font;
		_point white;
		_point atlas_size;
		float font_y;
		::std::vector<_vertex> vertices{};

		auto rectangle(_point minimum, _point maximum, _color color, _point uv_min, _point uv_max) -> void
		{
			auto const left = minimum.x * 2.0f / screen.x - 1.0f;
			auto const top = minimum.y * 2.0f / screen.y - 1.0f;
			auto const right = maximum.x * 2.0f / screen.x - 1.0f;
			auto const bottom = maximum.y * 2.0f / screen.y - 1.0f;
			vertices.append_range(::std::array<_vertex, 6>{
				_vertex{{left, top}, uv_min, color},
				_vertex{{right, top}, {uv_max.x, uv_min.y}, color},
				_vertex{{right, bottom}, uv_max, color},
				_vertex{{left, top}, uv_min, color},
				_vertex{{right, bottom}, uv_max, color},
				_vertex{{left, bottom}, {uv_min.x, uv_max.y}, color}});
		}

		auto rectangle(_point minimum, _point maximum, _color color) -> void
		{
			rectangle(minimum, maximum, color, white, white);
		}

		auto text(::std::string_view label, float size, float center_y, _color color, float available_width) -> void
		{
			if (label.empty())
				return;
			auto advance = 0.0f;
			auto top = (::std::numeric_limits<int>::max)();
			auto bottom = (::std::numeric_limits<int>::min)();
			for (auto character : label)
			{
				auto const& glyph = font.at(character);
				advance += glyph.advance;
				top = (::std::min)(top, glyph.y_offset);
				bottom = (::std::max)(bottom, glyph.y_offset + glyph.height);
			}
			auto const scale = (::std::min)(size / font.size, available_width / (::std::max)(advance, 1.0f));
			auto x = (screen.x - advance * scale) * 0.5f;
			auto const y = center_y - (top + bottom) * scale * 0.5f;
			for (auto character : label)
			{
				auto const& glyph = font.at(character);
				auto const left = x + glyph.x_offset * scale;
				auto const glyph_top = y + glyph.y_offset * scale;
				rectangle({left, glyph_top}, {left + glyph.width * scale, glyph_top + glyph.height * scale}, color,
					{glyph.x / atlas_size.x, (font_y + glyph.y) / atlas_size.y},
					{(glyph.x + glyph.width) / atlas_size.x, (font_y + glyph.y + glyph.height) / atlas_size.y});
				x += glyph.advance * scale;
			}
		}
	};

	[[nodiscard]] auto _draw(::VkExtent2D extent, state const& snapshot) const -> ::std::vector<_vertex>
	{
		auto draw = _geometry{
			.screen = {static_cast<float>(extent.width), static_cast<float>(extent.height)}, 
			.font = _graphic_resource.font, 
			.white = _graphic_resource.white, 
			.atlas_size = _graphic_resource.atlas_size,
			.font_y = _graphic_resource.font_y,
		};
		auto const screen = draw.screen;
		auto const image = _graphic_resource.image_size;
		auto const cover = (::std::max)(screen.x / image.x, screen.y / image.y);
		auto const uv_min = _graphic_resource.uv_min;
		auto const uv_max = _graphic_resource.uv_max;
		auto const crop = _point{(uv_max.x - uv_min.x) * (1.0f - screen.x / (image.x * cover)) * 0.5f,
			(uv_max.y - uv_min.y) * (1.0f - screen.y / (image.y * cover)) * 0.5f};
		draw.rectangle({0, 0}, screen, {0.03f, 0.04f, 0.07f, 1});
		draw.rectangle({0, 0}, screen, {1, 1, 1, 1},
			{uv_min.x + crop.x, uv_min.y + crop.y}, {uv_max.x - crop.x, uv_max.y - crop.y});
		draw.rectangle({0, 0}, screen, {0.02f, 0.03f, 0.06f, 0.32f});
		auto const count = static_cast<float>(_options.size());
		auto const scale = (::std::min)({screen.x / 960.0f, screen.y / 540.0f, screen.y / (count * 60.0f + 210.0f)});
		auto const width = 340.0f * scale;
		auto const row_height = 48.0f * scale;
		auto const gap = 12.0f * scale;
		auto const total = count * row_height + (count - 1.0f) * gap;
		auto const left = (screen.x - width) * 0.5f;
		auto const top = (screen.y - total) * 0.5f;
		auto const gold = _color{0.93f, 0.74f, 0.39f, 1};
		draw.rectangle({left - 28 * scale, top - 88 * scale},
			{left + width + 28 * scale, top + total + 84 * scale}, {0.03f, 0.045f, 0.075f, 0.92f});
		draw.rectangle({left - 28 * scale, top - 88 * scale},
			{left + width + 28 * scale, top - 85 * scale}, gold);
		draw.text(_title, 44 * scale, top - 44 * scale, gold, width);
		for (auto index = ::std::size_t{}; index < _options.size(); ++index)
		{
			auto const y = top + static_cast<float>(index) * (row_height + gap);
			auto const selected = index == snapshot.selected;
			auto const text_color = selected ? _color{0.08f, 0.06f, 0.04f, 1} : _color{0.9f, 0.92f, 0.95f, 1};
			draw.rectangle({left, y}, {left + width, y + row_height}, selected ? gold : _color{1, 1, 1, 0.06f});
			if (selected)
				draw.rectangle({left + 14 * scale, y + 20 * scale}, {left + 22 * scale, y + 28 * scale}, text_color);
			draw.text(_options[index], 28 * scale, y + row_height * 0.5f, text_color, width - 64 * scale);
		}
		draw.text("UP / DOWN - ENTER / SPACE", 16 * scale, top + total + 28 * scale,
			{0.65f, 0.7f, 0.78f, 1}, width);
		if (snapshot.activated)
			draw.text("SELECTED: " + _options[*snapshot.activated], 16 * scale,
				top + total + 56 * scale, gold, width);
		return ::std::move(draw.vertices);
	}

	::std::vector<::std::string> _options{"Start Game", "Settings", "Exit"};
	::std::string _title = "BVN";
	state _state{};
	int _held_direction = 0;
	::std::chrono::steady_clock::time_point _next_repeat{};

	inline static auto const resource_dir = ::std::filesystem::path{ __FILE__ }.parent_path() / "main_menu";
	inline static auto const font_path = resource_dir / "font.fnt";
	inline static auto const backgroud_path = resource_dir / "background.jpg";

	struct graphic_resource_t
	{
		_bitmap_font font{ font_path };
		_point 
			image_size{}
			, uv_min{}
			, uv_max{}
			, white{}
			, atlas_size{}
			;
		float font_y = 0;
		::vkkl::device_memory memory{};
		::vkkl::image image{};
		::vkkl::image_view view{};
		::vkkl::sampler sampler{};
		::vkkl::descriptor_set_layout descriptor_layout{};
		::vkkl::descriptor_pool descriptor_pool{};
		::vkkl::descriptor_set descriptor{};
		::vkkl::pipeline_layout layout{};
		::vkkl::pipeline pipeline{};
	} _graphic_resource;

	implementation(renderer& r)
	{
		namespace param = ::vkfu::param;
		using namespace ::vkfu::enums;

		auto source = ::bvn::assets::load_image(backgroud_path);
		auto font_image = ::bvn::assets::load_image(resource_dir / "font.png");
		if (
			font_image.width != static_cast<::std::uint32_t>(_graphic_resource.font.width) 
			|| font_image.height != static_cast<::std::uint32_t>(_graphic_resource.font.height)
			)
			throw ::std::runtime_error{ "main menu: font image dimensions differ from BMFont data" };

		auto global = r.vulkan.global_env();

		auto properties = ::vkfu::get_physical_device_properties2(global.physical_device()).head().properties;
		auto width = (::std::max)(source.width, font_image.width);
		auto height = static_cast<::std::uint64_t>(source.height) + font_image.height + 2;
		if (
			source.width == 0 
			|| source.height == 0 
			|| width > properties.limits.maxImageDimension2D 
			|| height > properties.limits.maxImageDimension2D
			)
			throw ::std::runtime_error{ "main menu: background and font exceed texture size limit" };

		auto row_bytes = static_cast<::std::size_t>(width) * 4;
		auto pixels = ::std::vector<::std::byte>(row_bytes * height);
		::std::ranges::fill_n(pixels.begin(), row_bytes, ::std::byte{ 255 });
		auto copy_rows = [&](::bvn::assets::image_rgba8 const& image, ::std::size_t first_row)
			{
				auto const stride = static_cast<::std::size_t>(image.width) * 4;
				assert(image.pixels.size() >= stride * image.height);
				for (auto row = ::std::size_t{}; row < image.height; ++row)
					::std::memcpy(pixels.data() + (first_row + row) * row_bytes, image.pixels.data() + row * stride, stride);
			};
		copy_rows(source, 1);
		copy_rows(font_image, static_cast<::std::size_t>(source.height) + 2);
		_graphic_resource.atlas_size = { static_cast<float>(width), static_cast<float>(height) };
		_graphic_resource.font_y = static_cast<float>(source.height) + 2;
		_graphic_resource.image_size = { static_cast<float>(source.width), static_cast<float>(source.height) };
		_graphic_resource.white = { 0.5f / _graphic_resource.atlas_size.x, 0.5f / _graphic_resource.atlas_size.y };
		_graphic_resource.uv_min = { 0.5f / _graphic_resource.atlas_size.x, 1.5f / _graphic_resource.atlas_size.y };
		_graphic_resource.uv_max = { (_graphic_resource.image_size.x - 0.5f) / _graphic_resource.atlas_size.x, (_graphic_resource.image_size.y + 0.5f) / _graphic_resource.atlas_size.y };

		auto staging = task::_buffer_type{ global.device(), global.physical_device(), pixels, {.transfer_src = 1} };
		auto const extent = ::VkExtent3D{ width, static_cast<::std::uint32_t>(height), 1 };
		auto const subresources = ::vkfu::evaluate(param::image_subresource_range{ .aspect_mask = {.color = 1}, .level_count = 1, .layer_count = 1 });
		_graphic_resource.image = ::vkkl::image{ 
			global.device(), 
			::vkfu::create_image(
				global.device()
				, param::image{
					.type = image_type::dim_2d, 
					.format = format::r8g8b8a8_srgb, 
					.extent = extent,
					.mip_levels = 1, 
					.array_layers = 1, 
					.samples = sample_count::count_1,
					.usage = {.transfer_dst = 1, .sampled = 1}
				}) 
		};
		auto requirements = ::vkfu::get_image_memory_requirements2(global.device(), param::image_memory_requirements2{ .image = _graphic_resource.image.handle }).head().memoryRequirements;

		auto memory_type = [](::VkPhysicalDevice physical, ::std::uint32_t allowed, ::VkMemoryPropertyFlags flags)
			{
				auto const properties = ::vkfu::get_physical_device_memory_properties2(physical).head().memoryProperties;
				for (auto index = ::std::uint32_t{}; index < properties.memoryTypeCount; ++index)
					if ((allowed & (1u << index)) && (properties.memoryTypes[index].propertyFlags & flags) == flags)
						return index;
				throw ::std::runtime_error{ "main menu: no compatible Vulkan memory type" };
			};

		_graphic_resource.memory = ::vkkl::device_memory{ global.device(), ::vkfu::allocate_memory(global.device(), param::memory{
			.allocation_size = requirements.size,
			.type_index = memory_type(global.physical_device(), requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)}) 
		};
		::vkfu::bind_image_memory2(global.device(), ::std::array{ ::vkfu::evaluate(param::bind_image_memory{.image = _graphic_resource.image.handle, .memory = _graphic_resource.memory.handle }) });

		auto pool = ::consumer_arch_vulkan::create_secondary_command_pool(global);
		auto command = _allocate_command(global.device(), pool.handle, command_buffer_level::primary);
		::vkfu::begin_command_buffer(command.handle, param::command_buffer_begin{ .flags = {.one_time_submit = 1} });
		::vkfu::cmd_pipeline_barrier2(command.handle, param::dependency{ .image_memory_barriers = ::std::array{ ::vkfu::evaluate(param::image_memory_barrier2{
			.src_stage_mask = {.top_of_pipe = 1},
			.dst_stage_mask = {.all_transfer = 1},
			.dst_access_mask = {.transfer_write = 1},
			.old_layout = image_layout::undefined,
			.new_layout = image_layout::transfer_dst_optimal,
			.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
			.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
			.image = _graphic_resource.image.handle,
			.subresource_range = subresources
		}) } });
		::vkfu::cmd_copy_buffer_to_image2(command.handle, param::copy_buffer_to_image2{
			.src_buffer = staging.handle.handle, 
			.dst_image = _graphic_resource.image.handle,
			.dst_image_layout = image_layout::transfer_dst_optimal, 
			.regions = ::std::array{ ::vkfu::evaluate(param::buffer_image_copy2{ .image_subresource = ::vkfu::evaluate(param::image_subresource_layers{.aspect_mask = {.color = 1}, .layer_count = 1}), .image_extent = extent }) } 
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
			.image = _graphic_resource.image.handle,
			.subresource_range = subresources }
			) } });
		::consumer_arch_vulkan::check(::vkEndCommandBuffer(command.handle), "main menu: end upload");
		auto fence = ::vkkl::fence{ global.device(), ::vkfu::create_fence(global.device(), param::fence{}) };
		::vkfu::queue_submit(global.graphics_queue(), ::std::array{ ::vkfu::evaluate(param::submit{.command_buffers = ::std::array{ command.handle } }) }, fence.handle);
		::vkfu::wait_for_fences(global.device(), ::std::array{ fence.handle }, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());

		_graphic_resource.view = ::vkkl::image_view{ 
			global.device(), 
			::vkfu::create_image_view(
				global.device(), 
				param::image_view{
					.image = _graphic_resource.image.handle, 
					.view_type = image_view_type::dim_2d,
					.format = format::r8g8b8a8_srgb, 
					.subresource_range = subresources
				}) 
		};
		_graphic_resource.sampler = ::vkkl::sampler{ 
			global.device(), 
			::vkfu::create_sampler(
				global.device()
				, param::sampler{
					.mag_filter = filter::linear, 
					.min_filter = filter::linear,
					.address_mode_u = sampler_address_mode::clamp_to_edge, 
					.address_mode_v = sampler_address_mode::clamp_to_edge,
					.address_mode_w = sampler_address_mode::clamp_to_edge
				}
				) 
		};

		_graphic_resource.descriptor_layout = ::vkkl::descriptor_set_layout{ 
			global.device(),
			::vkfu::create_descriptor_set_layout(global.device(), param::descriptor_set_layout{.bindings = ::std::array{ ::vkfu::evaluate(param::descriptor_set_layout_binding{
					.descriptor_type = descriptor_type::combined_image_sampler,
					.descriptor_count = 1,
					.stage_flags = {.fragment = 1}
				}) 
			}})
		};
		_graphic_resource.descriptor_pool = ::vkkl::descriptor_pool{ 
			global.device(), 
			::vkfu::create_descriptor_pool(
				global.device(),
				param::descriptor_pool{
					.flags = {.free_descriptor_set = 1}, 
					.max_sets = 1, 
					.pool_sizes = ::std::array{ ::vkfu::evaluate(param::descriptor_pool_size{
						.type = descriptor_type::combined_image_sampler,
						.descriptor_count = 1
					}) }
			}) 
		};
		auto raw_descriptor = ::VkDescriptorSet{};
		::vkfu::allocate_descriptor_sets(
			global.device(), 
			param::descriptor_set{
				.descriptor_pool = _graphic_resource.descriptor_pool.handle, 
				.set_layouts = ::std::array{ _graphic_resource.descriptor_layout.handle } 
			}, 
			::std::span{ &raw_descriptor, 1u }
			);
		_graphic_resource.descriptor = ::vkkl::descriptor_set{ global.device(), _graphic_resource.descriptor_pool.handle, raw_descriptor };
		auto const image_info = ::vkfu::evaluate(param::descriptor_image{
			.sampler = _graphic_resource.sampler.handle, 
			.image_view = _graphic_resource.view.handle, 
			.image_layout = image_layout::shader_read_only_optimal }
			);
		::vkfu::update_descriptor_sets(
			global.device()
			, ::std::array{ ::vkfu::evaluate(param::write_descriptor_set{
				.dst_set = raw_descriptor,
				.descriptor_count = 1,
				.descriptor_type = descriptor_type::combined_image_sampler,
				.image_info = &image_info }
				) }
			, {}
		);
		_graphic_resource.layout = ::vkkl::pipeline_layout{ 
			global.device(),
			::vkfu::create_pipeline_layout(
				global.device(), 
				param::pipeline_layout{.set_layouts = ::std::span{&_graphic_resource.descriptor_layout.handle, 1u}}
				) 
		};

		auto shader = [&](::std::span<::std::uint32_t const> code)
			{
				return ::vkkl::shader_module{ global.device(),
					::vkfu::create_shader_module(global.device(), param::shader_module{.code_size = code.size_bytes(), .code = code.data()}) };
			};
		auto vertex = shader(::consumer_arch_vulkan::read_spirv((resource_dir / "menu.vert.spv").string().c_str()));
		auto fragment = shader(::consumer_arch_vulkan::read_spirv((resource_dir / "menu.frag.spv").string().c_str()));
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
		_graphic_resource.pipeline = ::vkkl::pipeline{ 
			global.device(), 
			::vkfu::create_graphics_pipeline(global.device(), VK_NULL_HANDLE, param::graphics_pipeline{
				.stage_count = static_cast<::std::uint32_t>(stages.size()),
				.stages = stages.data(),
				.vertex_input_state = param::state::vertex_input{
					.vertex_binding_descriptions = ::std::array{ ::vkfu::evaluate(param::vertex_input_binding_description{.stride = sizeof(_vertex), .input_rate = vertex_input_rate::vertex }) },
					.vertex_attribute_descriptions = ::std::array{
						::vkfu::evaluate(param::vertex_input_attribute_description{.location = 0, .format = format::r32g32_sfloat, .offset = offsetof(_vertex, position)}),
						::vkfu::evaluate(param::vertex_input_attribute_description{.location = 1, .format = format::r32g32_sfloat, .offset = offsetof(_vertex, uv)}),
						::vkfu::evaluate(param::vertex_input_attribute_description{.location = 2, .format = format::r32g32b32a32_sfloat, .offset = offsetof(_vertex, color)})
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
				.layout = _graphic_resource.layout.handle,
			} | param::option::pipeline_rendering{.color_attachment_formats = ::std::array{ global.swapchain_image_format() } }
			) };
		}
};

main_menu::task::task(main_menu& menu, input::task& controls, renderer& r, renderer::task& draw) noexcept
	: _pool(::consumer_arch_vulkan::create_secondary_command_pool(r.vulkan.global_env()))
	, _update(values_sender_type<state, delta>{controls.poll() | ::stdexec::let_value([&](input::state const& current, input::delta const& changes)
		{
			auto change = menu._impl->_update(current, changes);
			return ::stdexec::just(menu._impl->_state, ::std::move(change));
		})} | ::exec::split())
{
}

main_menu::main_menu(renderer& r) : _impl(::std::make_unique<implementation>(r)) {}
main_menu::~main_menu() = default;

void main_menu::build_task(task& t, renderer& r, renderer::task& rt)
{
	auto const global = r.vulkan.global_env();
	auto&& menu = *_impl;

	rt.recorders.emplace_back(t.update()
		| ::stdexec::then([&, global](main_menu::state const& snapshot, main_menu::delta const&)
			{
				namespace param = ::vkfu::param;
				using namespace ::vkfu::enums;

				auto&& impl = menu;
				auto extent = global.swapchain_extent();

				assert(extent.width != 0 && extent.height != 0);
				auto const vertices = impl._draw(extent, snapshot);
				t._buffer.construct(global.device(), global.physical_device(), ::std::as_bytes(::std::span{ vertices }), param::buffer::usage_type{ .vertex_buffer = 1 });
				t._command = impl._allocate_command(global.device(), t._pool.handle, command_buffer_level::secondary);
				auto const command = t._command.handle;
				::vkfu::begin_command_buffer(command, param::command_buffer_begin{
					.flags = {.one_time_submit = 1, .render_pass_continue = 1},
					.inheritance_info =
						param::command_buffer_inheritance{}
						| param::option::command_buffer_inheritance_rendering{
							.color_attachment_formats = ::std::array{ global.swapchain_image_format() },
							.rasterization_samples = sample_count::count_1,
						}
					});
				auto const viewport = ::vkfu::evaluate(param::viewport{ .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .max_depth = 1.0f });
				auto const scissor = ::vkfu::evaluate(param::rect_2d{ .extent = extent });
				::vkfu::cmd_set_viewport(command, 0, ::std::array{ viewport });
				::vkfu::cmd_set_scissor(command, 0, ::std::span{ &scissor, 1u });
				::vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, impl._graphic_resource.pipeline.handle);
				::vkfu::cmd_bind_descriptor_sets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, impl._graphic_resource.layout.handle, 0, ::std::span{ &impl._graphic_resource.descriptor.handle, 1u }, {});
				auto const offset = ::VkDeviceSize{};
				::vkfu::cmd_bind_vertex_buffers(command, 0, ::std::span{ &t._buffer->handle.handle, 1u }, ::std::span{ &offset, 1u });
				assert(vertices.size() <= (::std::numeric_limits<::std::uint32_t>::max)());
				::vkCmdDraw(command, static_cast<::std::uint32_t>(vertices.size()), 1, 0, 0);
				::consumer_arch_vulkan::check(::vkEndCommandBuffer(command), "main menu: end draw");

				auto lock = ::std::scoped_lock{ rt.secondary.mutex };
				rt.secondary.commands.push_back(t._command);
			}));
}

main_menu::state const& main_menu::current_state() const noexcept
{
	return _impl->_state;
}
