#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "./drawing.h"

background::background(
	::bvn::assets::image_rgba8 img
	, VkDevice device
	, VkPhysicalDevice physical_device
	, VkQueue graphics_queue
	, ::std::uint32_t graphics_queue_family
	, VkFormat format
)
	: pipeline(device, format)
	, texture(::std::move(img), device, physical_device, graphics_queue, graphics_queue_family)
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

auto background::draw(
	VkDevice device
	, VkPhysicalDevice physical_device
	, VkCommandBuffer command_buffer
	, ::glm::mat3x3 transform
	) const noexcept -> buffer_type
{
	auto const extent = point{
		static_cast<float>(texture.source.width),
		static_cast<float>(texture.source.height)
	};

	auto transform_point = [&](point p) -> point
		{
			auto const result = transform * ::glm::vec3{ p.x, p.y, 1.0f };
			return { result.x, result.y };
		};

	auto const p0 = transform_point({ 0.0f, 0.0f });
	auto const p1 = transform_point({ extent.x, 0.0f });
	auto const p2 = transform_point({ extent.x, extent.y });
	auto const p3 = transform_point({ 0.0f, extent.y });

	geometry g;
	g.texture(p0, p1, p2, p3, { 0.0f, 0.0f }, { 1.0f, 1.0f });

	::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
	buffer_type result(device, physical_device, ::std::as_bytes(::std::span{ g.vertices }), ::vkfu::flags::buffer_usage{ .vertex_buffer = 1 });
	::vkfu::cmd_bind_descriptor_sets(command_buffer, ::vkfu::enums::pipeline_bind_point::graphics, pipeline.layout.handle, 0, ::std::array{ pipeline.descriptors[0].handle }, {});
	::vkfu::cmd_bind_vertex_buffers(command_buffer, 0, ::std::array{ result.handle.handle }, ::std::array{ ::VkDeviceSize{} });
	::vkCmdDraw(command_buffer, static_cast<::std::uint32_t>(g.vertices.size()), 1, 0, 0);
	return result;
}

vector_graph_type::vector_graph_type(
	VkDevice device
	, VkFormat format
)
	: pipeline(device, format)
{}

auto vector_graph_type::draw_rectangle(
	VkDevice device
	, VkPhysicalDevice physical_device
	, VkCommandBuffer command_buffer
	, point size
	, color color
	, ::glm::mat3x3 transform
) const noexcept -> buffer_type
{
	auto transform_point = [&](point p) -> point
		{
			auto const result = transform * ::glm::vec3{ p.x, p.y, 1.0f };
			return { result.x, result.y };
		};

	auto const p0 = transform_point({ 0.0f, 0.0f });
	auto const p1 = transform_point({ size.x, 0.0f });
	auto const p2 = transform_point({ size.x, size.y });
	auto const p3 = transform_point({ 0.0f, size.y });

	geometry g;
	g.rectangle(p0, p1, p2, p3, color);

	::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
	buffer_type result(device, physical_device, ::std::as_bytes(::std::span{ g.vertices }), ::vkfu::flags::buffer_usage{ .vertex_buffer = 1 });
	::vkfu::cmd_bind_vertex_buffers(command_buffer, 0, ::std::array{ result.handle.handle }, ::std::array{ ::VkDeviceSize{} });
	::vkCmdDraw(command_buffer, static_cast<::std::uint32_t>(g.vertices.size()), 1, 0, 0);
	return result;
}

auto _bitmap_font::_field(::std::string_view line, ::std::string_view name) -> int
{
	auto const offset = line.find(name);
	if (offset == ::std::string_view::npos)
		throw ::std::runtime_error{ "main menu: missing BMFont field " + ::std::string{name} };
	auto value = int{};
	auto const result = ::std::from_chars(line.data() + offset + name.size(), line.data() + line.size(), value);
	if (result.ec != ::std::errc{})
		throw ::std::runtime_error{ "main menu: invalid BMFont field " + ::std::string{name} };
	return value;
}

_bitmap_font::_bitmap_font(::std::filesystem::path const& path)
{
	auto file = ::std::ifstream{ path };
	if (!file)
		throw ::std::runtime_error{ "main menu: cannot open font " + path.string() };
	for (auto line = ::std::string{}; ::std::getline(file, line);)
	{
		if (line.starts_with("info "))
			size = _field(line, " size=");
		else if (line.starts_with("common "))
		{
			width = _field(line, " scaleW=");
			height = _field(line, " scaleH=");
			if (_field(line, " pages=") != 1 || _field(line, " packed=") != 0)
				throw ::std::runtime_error{ "main menu: font must use one unpacked image page" };
		}
		else if (line.starts_with("char "))
		{
			auto const id = _field(line, " id=");
			if (id < 0 || id >= static_cast<int>(glyphs.size()) || _field(line, " page=") != 0)
				throw ::std::runtime_error{ "main menu: unsupported font character or image page" };
			glyphs[id] = _glyph{ _field(line, " x="), _field(line, " y="), _field(line, " width="),
				_field(line, " height="), _field(line, " xoffset="), _field(line, " yoffset="), _field(line, " xadvance=") };
		}
	}
	if (!file.eof() || width <= 0 || height <= 0 || size <= 0)
		throw ::std::runtime_error{ "main menu: invalid BMFont data" };
	for (auto const& glyph : glyphs)
		if (glyph && (glyph->x < 0 || glyph->y < 0 || glyph->width < 0 || glyph->height < 0
			|| glyph->x > width - glyph->width || glyph->y > height - glyph->height || glyph->advance < 0))
			throw ::std::runtime_error{ "main menu: font glyph exceeds image bounds" };
}

auto _bitmap_font::at(char character) const -> _glyph const&
{
	auto const& glyph = glyphs[static_cast<unsigned char>(character)];
	if (!glyph)
		throw ::std::runtime_error{ "main menu: character is missing from font image" };
	return *glyph;
}

text_type::text_type(
	::bvn::assets::image_rgba8 img
	, VkDevice device
	, VkPhysicalDevice physical_device
	, VkQueue graphics_queue
	, ::std::uint32_t graphics_queue_family
	, VkFormat format
)
	: pipeline(device, format)
	, texture(::std::move(img), device, physical_device, graphics_queue, graphics_queue_family)
	, font(resource_dir / "font.fnt")
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

auto text_type::label(
	VkDevice device
	, VkPhysicalDevice physical_device
	, VkCommandBuffer command_buffer
	, ::std::string_view label
	, float size
	, float available_width
	, ::glm::mat3x3 transform
	) const -> buffer_type
{
	geometry g;

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
	auto x = -advance * scale * 0.5f;
	auto const y = -(top + bottom) * scale * 0.5f;
	auto const font_size = point{ static_cast<float>(font.width), static_cast<float>(font.height) };
	auto transform_point = [&](point p) -> point
		{
			auto const result = transform * ::glm::vec3{ p.x, p.y, 1.0f };
			return { result.x, result.y };
		};
	for (auto character : label)
	{
		auto const& glyph = font.at(character);
		auto const left = x + glyph.x_offset * scale;
		auto const glyph_top = y + glyph.y_offset * scale;
		g.texture(
			transform_point({ left, glyph_top })
			, transform_point({ left + glyph.width * scale, glyph_top })
			, transform_point({ left + glyph.width * scale, glyph_top + glyph.height * scale })
			, transform_point({ left, glyph_top + glyph.height * scale })
			, { glyph.x / font_size.x, glyph.y / font_size.y }
			, { (glyph.x + glyph.width) / font_size.x, (glyph.y + glyph.height) / font_size.y }
		);
		x += glyph.advance * scale;
	}

	::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
	buffer_type result(device, physical_device, ::std::as_bytes(::std::span{ g.vertices }), ::vkfu::flags::buffer_usage{ .vertex_buffer = 1 });
	::vkfu::cmd_bind_descriptor_sets(command_buffer, ::vkfu::enums::pipeline_bind_point::graphics, pipeline.layout.handle, 0, ::std::array{ pipeline.descriptors[0].handle }, {});
	::vkfu::cmd_bind_vertex_buffers(command_buffer, 0, ::std::array{ result.handle.handle }, ::std::array{ ::VkDeviceSize{} });
	::vkCmdDraw(command_buffer, static_cast<::std::uint32_t>(g.vertices.size()), 1, 0, 0);
	return result;
}
