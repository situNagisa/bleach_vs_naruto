#pragma once

#include <array>
#include <vector>
#include <cassert>

struct point
{
	float x, y;
};
using color = ::std::array<float, 4>;
struct vertex_type
{
	point position, uv;
	color color;
	::std::uint32_t textured;
};

struct geometry
{
	::std::vector<vertex_type> vertices{};

	constexpr static point to_ndc(point screen, point position) noexcept
	{
		return {
			position.x * 2.0f / screen.x - 1.0f,
			position.y * 2.0f / screen.y - 1.0f
		};
	}

	auto texture_rectangle(point minimum, point maximum, point uv_min = { 0.0f, 0.0f }, point uv_max = { 1.0f, 1.0f }) -> void
	{
		assert(vertices.size() <= (::std::numeric_limits<::std::uint32_t>::max)() - 6u);
		auto tint = color{ 1.0f, 1.0f, 1.0f, 1.0f };
		texture(minimum, { maximum.x, minimum.y }, maximum, { minimum.x, maximum.y }, uv_min, uv_max);
	}

	auto texture(point lt, point rt, point rb, point lb, point uv_min = { 0.0f, 0.0f }, point uv_max = { 1.0f, 1.0f }) -> void
	{
		assert(vertices.size() <= (::std::numeric_limits<::std::uint32_t>::max)() - 6u);
		auto tint = color{ 1.0f, 1.0f, 1.0f, 1.0f };
		vertices.append_range(::std::array{
			vertex_type{ lt, uv_min, tint, 1 },
			vertex_type{ rt, {uv_max.x, uv_min.y}, tint, 1 },
			vertex_type{ rb, uv_max, tint, 1 },
			vertex_type{ lt, uv_min, tint, 1 },
			vertex_type{ rb, uv_max, tint, 1 },
			vertex_type{ lb, {uv_min.x, uv_max.y}, tint, 1 } });
	}

	auto rectangle(point minimum, point maximum, color color) -> void
	{
		assert(vertices.size() <= (::std::numeric_limits<::std::uint32_t>::max)() - 6u);
		vertices.append_range(::std::array{
			vertex_type{ minimum, {}, color, 0 },
			vertex_type{ {maximum.x, minimum.y}, {}, color, 0 },
			vertex_type{ maximum, {}, color, 0 },
			vertex_type{ minimum, {}, color, 0 },
			vertex_type{ maximum, {}, color, 0 },
			vertex_type{ {minimum.x, maximum.y}, {}, color, 0 } });
	}

	auto rectangle(point lt, point rt, point rb, point lb, color color) -> void
	{
		assert(vertices.size() <= (::std::numeric_limits<::std::uint32_t>::max)() - 6u);
		vertices.append_range(::std::array{
			vertex_type{ lt, {}, color, 0 },
			vertex_type{ rt, {}, color, 0 },
			vertex_type{ rb, {}, color, 0 },
			vertex_type{ lt, {}, color, 0 },
			vertex_type{ rb, {}, color, 0 },
			vertex_type{ lb, {}, color, 0 } });
	}
};
