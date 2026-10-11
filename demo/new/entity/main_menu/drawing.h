#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <filesystem>
#include <optional>
#include <string_view>

#include <glm/glm.hpp>

#include "./geometry.h"
#include "./buffer_type.h"
#include "./texture.h"
#include "./graphic_resource.h"

struct background
{
	graphic_resource_type pipeline;
	texture_type texture;

	background(::bvn::assets::image_rgba8 img, VkDevice device, VkPhysicalDevice physical_device,
		VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, VkFormat format);

	auto draw(VkDevice device, VkPhysicalDevice physical_device, VkCommandBuffer command_buffer,
		point viewport, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }) const noexcept -> buffer_type;
};

struct vector_graph_type
{
	graphic_resource_type pipeline;

	vector_graph_type(VkDevice device, VkFormat format);

	auto draw_rectangle(VkDevice device, VkPhysicalDevice physical_device, VkCommandBuffer command_buffer,
		point viewport, point size, color color, ::glm::mat3x3 transform = ::glm::mat3{ 1.0f }) const noexcept -> buffer_type;
};

struct _glyph
{
	int x, y, width, height, x_offset, y_offset, advance;
};

struct _bitmap_font
{
	::std::array<::std::optional<_glyph>, 256> glyphs{};
	int width = 0, height = 0, size = 0;

	static auto _field(::std::string_view line, ::std::string_view name) -> int;
	explicit _bitmap_font(::std::filesystem::path const& path);
	[[nodiscard]] auto at(char character) const -> _glyph const&;
};

struct text_type
{
	graphic_resource_type pipeline;
	texture_type texture;
	_bitmap_font font;

	text_type(::bvn::assets::image_rgba8 img, VkDevice device, VkPhysicalDevice physical_device,
		VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, VkFormat format);

	auto label(VkDevice device, VkPhysicalDevice physical_device, VkCommandBuffer command_buffer,
		point screen, ::std::string_view label, float size, float center_y, float available_width) const -> buffer_type;
};
