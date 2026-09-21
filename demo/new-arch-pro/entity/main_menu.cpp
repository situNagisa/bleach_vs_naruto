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
#include "./detail/immovable.h"

struct main_menu::implementation : immovable
{
	struct _update_fn
	{
		implementation* menu;
		auto operator()(input::state const& current, input::delta const& changes) const
			-> decltype(::stdexec::just(state{}, delta{}))
		{
			auto change = menu->_update(current, changes);
			return ::stdexec::just(menu->_state, ::std::move(change));
		}
	};

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

	static auto _memory_type(::VkPhysicalDevice physical, ::std::uint32_t allowed,
		::VkMemoryPropertyFlags flags) -> ::std::uint32_t
	{
		auto const properties = ::vkfu::get_physical_device_memory_properties2(physical).head().memoryProperties;
		for (auto index = ::std::uint32_t{}; index < properties.memoryTypeCount; ++index)
			if ((allowed & (1u << index)) && (properties.memoryTypes[index].propertyFlags & flags) == flags)
				return index;
		throw ::std::runtime_error{"main menu: no compatible Vulkan memory type"};
	}

	struct _buffer : immovable
	{
		::VkDevice _device;
		::vkkl::device_memory _memory{};
		::vkkl::buffer _handle{};

		_buffer(::consumer_arch_vulkan::global_vulkan_env_renderer global,
			::std::span<::std::byte const> data, ::vkfu::param::buffer::usage_type usage) : _device(global.device())
		{
			namespace param = ::vkfu::param;

			assert(!data.empty());
			_handle = ::vkkl::buffer{_device, ::vkfu::create_buffer(_device, param::buffer{.size = data.size(), .usage = usage})};
			auto const requirements = ::vkfu::get_buffer_memory_requirements2(_device,
				param::buffer_memory_requirements2{.buffer = _handle.handle}).head().memoryRequirements;
			_memory = ::vkkl::device_memory{_device, ::vkfu::allocate_memory(_device, param::memory{
				.allocation_size = requirements.size,
				.type_index = _memory_type(global.physical_device(), requirements.memoryTypeBits,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)})};
			auto const bind = ::vkfu::evaluate(param::bind_buffer_memory{.buffer = _handle.handle, .memory = _memory.handle});
			::vkfu::bind_buffer_memory2(_device, ::std::span{&bind, 1u});
			auto mapped = static_cast<void*>(nullptr);
			::consumer_arch_vulkan::check(::vkMapMemory(_device, _memory.handle, 0, data.size(), 0, &mapped), "main menu: map buffer");
			::std::memcpy(mapped, data.data(), data.size());
			::vkUnmapMemory(_device, _memory.handle);
		}
	};

	static auto _allocate_command(::VkDevice device, ::VkCommandPool pool, ::vkfu::enums::command_buffer_level level)
		-> ::vkkl::command_buffer
	{
		auto raw = ::VkCommandBuffer{};
		::vkfu::allocate_command_buffers(device,
			::vkfu::param::command_buffer{.command_pool = pool, .level = level, .command_buffer_count = 1}, ::std::span{&raw, 1u});
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

	struct _graphics_resources : immovable
	{
		::VkDevice _device;
		::VkFormat _format;
		_point _image_size{}, _uv_min{}, _uv_max{}, _white{}, _atlas_size{};
		float _font_y = 0;
		_bitmap_font _font;
		::vkkl::device_memory _memory{};
		::vkkl::image _image{};
		::vkkl::image_view _view{};
		::vkkl::sampler _sampler{};
		::vkkl::descriptor_set_layout _descriptor_layout{};
		::vkkl::descriptor_pool _descriptor_pool{};
		::vkkl::descriptor_set _descriptor{};
		::vkkl::pipeline_layout _layout{};
		::vkkl::pipeline _pipeline{};

		_graphics_resources(::consumer_arch_vulkan::global_vulkan_env_renderer global, ::std::filesystem::path const& path,
			::std::filesystem::path const& shaders)
			: _device(global.device()), _format(global.swapchain_image_format()), _font(shaders / "font.fnt")
		{
			_upload(global, path, shaders / "font.png");
			_create_pipeline(shaders);
		}

		auto _upload(::consumer_arch_vulkan::global_vulkan_env_renderer global, ::std::filesystem::path const& path,
			::std::filesystem::path const& font_path) -> void
		{
			namespace param = ::vkfu::param;
			using namespace ::vkfu::enums;

			auto const source = ::bvn::assets::load_sprite_clip(path);
			auto const font_image = ::bvn::assets::load_sprite_clip(font_path);
			if (font_image.frame_width != static_cast<::std::uint32_t>(_font.width)
				|| font_image.frame_height != static_cast<::std::uint32_t>(_font.height))
				throw ::std::runtime_error{"main menu: font image dimensions differ from BMFont data"};
			auto const properties = ::vkfu::get_physical_device_properties2(global.physical_device()).head().properties;
			auto const width = (::std::max)(source.frame_width, font_image.frame_width);
			auto const height = static_cast<::std::uint64_t>(source.frame_height) + font_image.frame_height + 2;
			if (source.frame_width == 0 || source.frame_height == 0 || width > properties.limits.maxImageDimension2D
				|| height > properties.limits.maxImageDimension2D)
				throw ::std::runtime_error{"main menu: background and font exceed texture size limit"};
			auto const row_bytes = static_cast<::std::size_t>(width) * 4;
			auto pixels = ::std::vector<::std::byte>(row_bytes * height);
			::std::fill_n(pixels.begin(), row_bytes, ::std::byte{255});
			auto copy_rows = [&](auto const& image, ::std::size_t first_row)
				{
					auto const stride = static_cast<::std::size_t>(image.frame_width) * 4;
					assert(image.frames_rgba8.size() >= stride * image.frame_height);
					for (auto row = ::std::size_t{}; row < image.frame_height; ++row)
						::std::memcpy(pixels.data() + (first_row + row) * row_bytes, image.frames_rgba8.data() + row * stride, stride);
				};
			copy_rows(source, 1);
			copy_rows(font_image, static_cast<::std::size_t>(source.frame_height) + 2);
			_atlas_size = {static_cast<float>(width), static_cast<float>(height)};
			_font_y = static_cast<float>(source.frame_height) + 2;
			_image_size = {static_cast<float>(source.frame_width), static_cast<float>(source.frame_height)};
			_white = {0.5f / _atlas_size.x, 0.5f / _atlas_size.y};
			_uv_min = {0.5f / _atlas_size.x, 1.5f / _atlas_size.y};
			_uv_max = {(_image_size.x - 0.5f) / _atlas_size.x, (_image_size.y + 0.5f) / _atlas_size.y};

			auto staging = _buffer{global, pixels, {.transfer_src = 1}};
			auto const extent = ::VkExtent3D{width, static_cast<::std::uint32_t>(height), 1};
			auto const subresources = ::vkfu::evaluate(param::image_subresource_range{
				.aspect_mask = {.color = 1}, .level_count = 1, .layer_count = 1});
			_image = ::vkkl::image{_device, ::vkfu::create_image(_device, param::image{
				.type = image_type::dim_2d, .format = format::r8g8b8a8_srgb, .extent = extent,
				.mip_levels = 1, .array_layers = 1, .samples = sample_count::count_1,
				.usage = {.transfer_dst = 1, .sampled = 1}})};
			auto const requirements = ::vkfu::get_image_memory_requirements2(_device,
				param::image_memory_requirements2{.image = _image.handle}).head().memoryRequirements;
			_memory = ::vkkl::device_memory{_device, ::vkfu::allocate_memory(_device, param::memory{
				.allocation_size = requirements.size,
				.type_index = _memory_type(global.physical_device(), requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)})};
			auto const bind = ::vkfu::evaluate(param::bind_image_memory{.image = _image.handle, .memory = _memory.handle});
			::vkfu::bind_image_memory2(_device, ::std::span{&bind, 1u});

			auto pool = ::consumer_arch_vulkan::create_secondary_command_pool(global);
			auto command = _allocate_command(_device, pool.handle, command_buffer_level::primary);
			::vkfu::begin_command_buffer(command.handle, param::command_buffer_begin{.flags = {.one_time_submit = 1}});
			auto const to_transfer = ::vkfu::evaluate(param::image_memory_barrier2{
				.src_stage_mask = {.top_of_pipe = 1},
				.dst_stage_mask = {.all_transfer = 1}, .dst_access_mask = {.transfer_write = 1},
				.old_layout = image_layout::undefined, .new_layout = image_layout::transfer_dst_optimal,
				.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED, .dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
				.image = _image.handle, .subresource_range = subresources});
			::vkfu::cmd_pipeline_barrier2(command.handle, param::dependency{.image_memory_barriers = ::std::span{&to_transfer, 1u}});
			auto const copy = ::vkfu::evaluate(param::buffer_image_copy2{
				.image_subresource = ::vkfu::evaluate(param::image_subresource_layers{.aspect_mask = {.color = 1}, .layer_count = 1}),
				.image_extent = extent});
			::vkfu::cmd_copy_buffer_to_image2(command.handle, param::copy_buffer_to_image2{
				.src_buffer = staging._handle.handle, .dst_image = _image.handle,
				.dst_image_layout = image_layout::transfer_dst_optimal, .regions = ::std::span{&copy, 1u}});
			auto const to_sampled = ::vkfu::evaluate(param::image_memory_barrier2{
				.src_stage_mask = {.all_transfer = 1}, .src_access_mask = {.transfer_write = 1},
				.dst_stage_mask = {.fragment_shader = 1}, .dst_access_mask = {.shader_read = 1},
				.old_layout = image_layout::transfer_dst_optimal, .new_layout = image_layout::shader_read_only_optimal,
				.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED, .dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
				.image = _image.handle, .subresource_range = subresources});
			::vkfu::cmd_pipeline_barrier2(command.handle, param::dependency{.image_memory_barriers = ::std::span{&to_sampled, 1u}});
			::consumer_arch_vulkan::check(::vkEndCommandBuffer(command.handle), "main menu: end upload");
			auto fence = ::vkkl::fence{_device, ::vkfu::create_fence(_device, param::fence{})};
			auto const submit = ::vkfu::evaluate(param::submit{.command_buffers = ::std::span{&command.handle, 1u}});
			::vkfu::queue_submit(global.graphics_queue(), ::std::span{&submit, 1u}, fence.handle);
			::vkfu::wait_for_fences(_device, ::std::span{&fence.handle, 1u}, VK_TRUE, (::std::numeric_limits<::std::uint64_t>::max)());

			_view = ::vkkl::image_view{_device, ::vkfu::create_image_view(_device, param::image_view{
				.image = _image.handle, .view_type = image_view_type::dim_2d,
				.format = format::r8g8b8a8_srgb, .subresource_range = subresources})};
			_sampler = ::vkkl::sampler{_device, ::vkfu::create_sampler(_device, param::sampler{
				.mag_filter = filter::linear, .min_filter = filter::linear,
				.address_mode_u = sampler_address_mode::clamp_to_edge, .address_mode_v = sampler_address_mode::clamp_to_edge,
				.address_mode_w = sampler_address_mode::clamp_to_edge})};
		}

		auto _create_pipeline(::std::filesystem::path const& shaders) -> void;
	};

	struct _recording : immovable
	{
		// 与本帧任务一同销毁，vertex buffer 不会被后续帧覆盖。
		::std::unique_ptr<_buffer> _vertices{};
		::vkkl::command_pool _pool;
		::vkkl::command_buffer _command{};
		explicit _recording(::consumer_arch_vulkan::global_vulkan_env_renderer global)
			: _pool(::consumer_arch_vulkan::create_secondary_command_pool(global)) {}
	};

	struct _geometry
	{
		_point screen;
		_graphics_resources const& graphics;
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
			rectangle(minimum, maximum, color, graphics._white, graphics._white);
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
				auto const& glyph = graphics._font.at(character);
				advance += glyph.advance;
				top = (::std::min)(top, glyph.y_offset);
				bottom = (::std::max)(bottom, glyph.y_offset + glyph.height);
			}
			auto const scale = (::std::min)(size / graphics._font.size, available_width / (::std::max)(advance, 1.0f));
			auto x = (screen.x - advance * scale) * 0.5f;
			auto const y = center_y - (top + bottom) * scale * 0.5f;
			for (auto character : label)
			{
				auto const& glyph = graphics._font.at(character);
				auto const left = x + glyph.x_offset * scale;
				auto const glyph_top = y + glyph.y_offset * scale;
				rectangle({left, glyph_top}, {left + glyph.width * scale, glyph_top + glyph.height * scale}, color,
					{glyph.x / graphics._atlas_size.x, (graphics._font_y + glyph.y) / graphics._atlas_size.y},
					{(glyph.x + glyph.width) / graphics._atlas_size.x, (graphics._font_y + glyph.y + glyph.height) / graphics._atlas_size.y});
				x += glyph.advance * scale;
			}
		}
	};

	[[nodiscard]] auto _draw(::VkExtent2D extent, state const& snapshot) const -> ::std::vector<_vertex>
	{
		auto draw = _geometry{{static_cast<float>(extent.width), static_cast<float>(extent.height)}, *_graphics};
		auto const screen = draw.screen;
		auto const image = _graphics->_image_size;
		auto const cover = (::std::max)(screen.x / image.x, screen.y / image.y);
		auto const uv_min = _graphics->_uv_min;
		auto const uv_max = _graphics->_uv_max;
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

	auto _record(::consumer_arch_vulkan::global_vulkan_env_renderer global, ::VkExtent2D extent,
		_recording& recording, state const& snapshot) const -> void
	{
		namespace param = ::vkfu::param;
		using namespace ::vkfu::enums;

		assert(extent.width != 0 && extent.height != 0);
		auto const vertices = _draw(extent, snapshot);
		recording._vertices = ::std::make_unique<_buffer>(global, ::std::as_bytes(::std::span{vertices}), param::buffer::usage_type{.vertex_buffer = 1});
		recording._command = _allocate_command(global.device(), recording._pool.handle, command_buffer_level::secondary);
		auto const command = recording._command.handle;
		::vkfu::begin_command_buffer(command, param::command_buffer_begin{
			.flags = {.one_time_submit = 1, .render_pass_continue = 1},
			.inheritance_info = param::command_buffer_inheritance{} | param::option::command_buffer_inheritance_rendering{
				.color_attachment_formats = ::std::span{&_graphics->_format, 1u}, .rasterization_samples = sample_count::count_1}});
		auto const viewport = ::vkfu::evaluate(param::viewport{
			.width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .max_depth = 1.0f});
		auto const scissor = ::vkfu::evaluate(param::rect_2d{.extent = extent});
		::vkfu::cmd_set_viewport(command, 0, ::std::span{&viewport, 1u});
		::vkfu::cmd_set_scissor(command, 0, ::std::span{&scissor, 1u});
		::vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphics->_pipeline.handle);
		::vkfu::cmd_bind_descriptor_sets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, _graphics->_layout.handle,
			0, ::std::span{&_graphics->_descriptor.handle, 1u}, {});
		auto const offset = ::VkDeviceSize{};
		::vkfu::cmd_bind_vertex_buffers(command, 0, ::std::span{&recording._vertices->_handle.handle, 1u}, ::std::span{&offset, 1u});
		assert(vertices.size() <= (::std::numeric_limits<::std::uint32_t>::max)());
		::vkCmdDraw(command, static_cast<::std::uint32_t>(vertices.size()), 1, 0, 0);
		::consumer_arch_vulkan::check(::vkEndCommandBuffer(command), "main menu: end draw");
	}

	::std::filesystem::path _background_path = ::std::filesystem::path{__FILE__}.parent_path() / "main_menu/background.jpg";
	::std::vector<::std::string> _options{"Start Game", "Settings", "Exit"};
	::std::string _title = "BVN";
	::std::filesystem::path _resource_directory = ::std::filesystem::path{__FILE__}.parent_path() / "main_menu";
	state _state{};
	int _held_direction = 0;
	::std::chrono::steady_clock::time_point _next_repeat{};
	::std::unique_ptr<_graphics_resources> _graphics{};

};

auto main_menu::implementation::_graphics_resources::_create_pipeline(::std::filesystem::path const& shaders) -> void
{
	namespace param = ::vkfu::param;
	using namespace ::vkfu::enums;

	auto const binding = ::vkfu::evaluate(param::descriptor_set_layout_binding{
		.descriptor_type = descriptor_type::combined_image_sampler, .descriptor_count = 1, .stage_flags = {.fragment = 1}});
	_descriptor_layout = ::vkkl::descriptor_set_layout{_device,
		::vkfu::create_descriptor_set_layout(_device, param::descriptor_set_layout{.bindings = ::std::span{&binding, 1u}})};
	auto const pool_size = ::vkfu::evaluate(param::descriptor_pool_size{.type = descriptor_type::combined_image_sampler, .descriptor_count = 1});
	_descriptor_pool = ::vkkl::descriptor_pool{_device, ::vkfu::create_descriptor_pool(_device,
		param::descriptor_pool{.flags = {.free_descriptor_set = 1}, .max_sets = 1, .pool_sizes = ::std::span{&pool_size, 1u}})};
	auto raw_descriptor = ::VkDescriptorSet{};
	::vkfu::allocate_descriptor_sets(_device, param::descriptor_set{
		.descriptor_pool = _descriptor_pool.handle, .set_layouts = ::std::span{&_descriptor_layout.handle, 1u}}, ::std::span{&raw_descriptor, 1u});
	_descriptor = ::vkkl::descriptor_set{_device, _descriptor_pool.handle, raw_descriptor};
	auto const image_info = ::vkfu::evaluate(param::descriptor_image{
		.sampler = _sampler.handle, .image_view = _view.handle, .image_layout = image_layout::shader_read_only_optimal});
	auto const write = ::vkfu::evaluate(param::write_descriptor_set{
		.dst_set = raw_descriptor, .descriptor_count = 1, .descriptor_type = descriptor_type::combined_image_sampler, .image_info = &image_info});
	::vkfu::update_descriptor_sets(_device, ::std::span{&write, 1u}, {});
	_layout = ::vkkl::pipeline_layout{_device,
		::vkfu::create_pipeline_layout(_device, param::pipeline_layout{.set_layouts = ::std::span{&_descriptor_layout.handle, 1u}})};

	auto shader = [this](::std::span<::std::uint32_t const> code)
		{
			return ::vkkl::shader_module{_device,
				::vkfu::create_shader_module(_device, param::shader_module{.code_size = code.size_bytes(), .code = code.data()})};
		};
	auto vertex = shader(::consumer_arch_vulkan::read_spirv((shaders / "menu.vert.spv").string().c_str()));
	auto fragment = shader(::consumer_arch_vulkan::read_spirv((shaders / "menu.frag.spv").string().c_str()));
	auto const stages = ::std::array{
		::vkfu::evaluate(param::state::shader_stage{.stage = shader_stage::vertex, .module = vertex.handle, .name = "main"}),
		::vkfu::evaluate(param::state::shader_stage{.stage = shader_stage::fragment, .module = fragment.handle, .name = "main"})};
	auto const vertex_binding = ::vkfu::evaluate(param::vertex_input_binding_description{.stride = sizeof(_vertex), .input_rate = vertex_input_rate::vertex});
	auto const attributes = ::std::array{
		::vkfu::evaluate(param::vertex_input_attribute_description{.location = 0, .format = format::r32g32_sfloat, .offset = offsetof(_vertex, position)}),
		::vkfu::evaluate(param::vertex_input_attribute_description{.location = 1, .format = format::r32g32_sfloat, .offset = offsetof(_vertex, uv)}),
		::vkfu::evaluate(param::vertex_input_attribute_description{.location = 2, .format = format::r32g32b32a32_sfloat, .offset = offsetof(_vertex, color)})};
	auto const attachment = ::vkfu::evaluate(param::state::color_blend_attachment{
		.blend_enable = true,
		.src_color_blend_factor = blend_factor::src_alpha, .dst_color_blend_factor = blend_factor::one_minus_src_alpha,
		.src_alpha_blend_factor = blend_factor::one, .dst_alpha_blend_factor = blend_factor::one_minus_src_alpha,
		.color_write_mask = {.r = 1, .g = 1, .b = 1, .a = 1}});
	auto const dynamic_states = ::std::array{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	auto info = param::graphics_pipeline{
		.stage_count = static_cast<::std::uint32_t>(stages.size()), .stages = stages.data(),
		.vertex_input_state = param::state::vertex_input{
			.vertex_binding_descriptions = ::std::span{&vertex_binding, 1u}, .vertex_attribute_descriptions = attributes},
		.input_assembly_state = param::state::input_assembly{.topology = primitive_topology::triangle_list},
		.viewport_state = param::state::viewport{.viewport_count = 1, .scissor_count = 1},
		.rasterization_state = param::state::rasterization{
			.polygon_mode = polygon_mode::fill, .front_face = front_face::counter_clockwise, .line_width = 1.0f},
		.multisample_state = param::state::multisample{.rasterization_samples = sample_count::count_1},
		.color_blend_state = param::state::color_blend{.attachment_count = 1, .attachments = &attachment},
		.dynamic_state = param::state::dynamic{.states = dynamic_states},
		.layout = _layout.handle,
	} | param::option::pipeline_rendering{.color_attachment_formats = ::std::span{&_format, 1u}};
	_pipeline = ::vkkl::pipeline{_device, ::vkfu::create_graphics_pipeline(_device, VK_NULL_HANDLE, info)};
}

struct main_menu::task::implementation : immovable
{
	main_menu::implementation::_recording recording;
	_update_sender shared;

	implementation(::consumer_arch_vulkan::global_vulkan_env_renderer global, _update_sender update)
		: recording(global), shared(::std::move(update))
	{}
};

main_menu::task::task(::std::unique_ptr<implementation> impl) noexcept : _impl(::std::move(impl)) {}
main_menu::task::task(task&&) noexcept = default;
main_menu::task::~task() = default;

auto main_menu::task::update() const noexcept -> _update_sender
{
	assert(_impl != nullptr);
	return _impl->shared;
}

main_menu::main_menu() : _impl(::std::make_unique<implementation>()) {}
main_menu::~main_menu() = default;

auto main_menu::build_task(frame_context&, entity_view<frame_context> view, task_builder builder) -> void
{
	auto controls = view.entity<input>();
	auto draw = view.entity<renderer>();
	assert(controls.has_value() && "main menu requires input");
	assert(draw.has_value() && "main menu requires renderer");
	auto input_task = controls->task<input::task>();
	auto render_state = draw->task<renderer::state>();
	assert(input_task != nullptr && render_state != nullptr);
	auto const global = draw->get().vulkan.global_env();
	auto&& menu = *_impl;
	if (!menu._graphics)
		menu._graphics = ::std::make_unique<implementation::_graphics_resources>(global, menu._background_path, menu._resource_directory);
	assert(menu._graphics->_device == global.device());
	assert(menu._graphics->_format == global.swapchain_image_format());

	auto update = _sender{input_task->poll() | ::stdexec::let_value(implementation::_update_fn{&menu})} | ::exec::split();
	auto&& menu_task = builder.emplace<task>(task{::std::make_unique<task::implementation>(global, ::std::move(update))});
	auto&& recording = menu_task._impl->recording;
	// renderer 在 begin 完成后启动 recorders，task 持有录制资源直到本帧结束。
	render_state->recorders.emplace_back(menu_task.update()
		| ::stdexec::then([&menu, &recording, render_state, global](main_menu::state const& snapshot, main_menu::delta const&)
			{
				menu._record(global, global.swapchain_extent(), recording, snapshot);
				auto lock = ::std::scoped_lock{render_state->secondary.mutex};
				render_state->secondary.commands.push_back(recording._command);
			}));
}
