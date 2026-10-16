#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm/glm.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

#include <vkkl/command_buffer.h>
#include <vkkl/command_pool.h>

#include "../frame_clock.h"
#include "../input.h"
#include "../render.h"
#include "../main_menu/buffer_type.h"
#include "../main_menu/geometry.h"
#include "../main_menu/text.h"
#include "../main_menu/vector_graph.h"

/// 菜单类场景共用的小部件：布局、缓动、按键重复、淡入淡出、画笔。
/// 纯业务代码，不属于框架——main_menu / setting_scene 都用它，别的场景可以不用。
namespace ui
{
	struct rect
	{
		float left = 0.0f, top = 0.0f, width = 0.0f, height = 0.0f;

		[[nodiscard]] constexpr auto right() const noexcept -> float { return left + width; }
		[[nodiscard]] constexpr auto bottom() const noexcept -> float { return top + height; }
		[[nodiscard]] constexpr auto center() const noexcept -> point { return { left + width * 0.5f, top + height * 0.5f }; }
		[[nodiscard]] constexpr auto contains(point p) const noexcept -> bool
		{
			return p.x >= left && p.x < right() && p.y >= top && p.y < bottom();
		}
		[[nodiscard]] constexpr auto inset(float dx, float dy) const noexcept -> rect
		{
			return { left + dx, top + dy, width - 2.0f * dx, height - 2.0f * dy };
		}
	};

	namespace palette
	{
		inline constexpr auto gold = color{ 0.93f, 0.74f, 0.39f, 1.0f };
		inline constexpr auto gold_dim = color{ 0.93f, 0.74f, 0.39f, 0.18f };
		inline constexpr auto ink = color{ 0.08f, 0.06f, 0.04f, 1.0f };
		inline constexpr auto paper = color{ 0.9f, 0.92f, 0.95f, 1.0f };
		inline constexpr auto muted = color{ 0.58f, 0.62f, 0.7f, 1.0f };
		inline constexpr auto panel = color{ 0.03f, 0.045f, 0.075f, 0.92f };
		inline constexpr auto panel_edge = color{ 1.0f, 1.0f, 1.0f, 0.08f };
		inline constexpr auto row = color{ 1.0f, 1.0f, 1.0f, 0.06f };
		inline constexpr auto hover = color{ 1.0f, 1.0f, 1.0f, 0.05f };
		inline constexpr auto shade = color{ 0.02f, 0.03f, 0.06f, 0.42f };
		inline constexpr auto black = color{ 0.0f, 0.0f, 0.0f, 1.0f };
		inline constexpr auto white = color{ 1.0f, 1.0f, 1.0f, 1.0f };
	}

	[[nodiscard]] constexpr auto with_alpha(color c, float alpha) noexcept -> color
	{
		return { c[0], c[1], c[2], alpha };
	}

	[[nodiscard]] constexpr auto mix(color a, color b, float t) noexcept -> color
	{
		return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t };
	}

	[[nodiscard]] constexpr auto clamp01(float t) noexcept -> float { return ::std::clamp(t, 0.0f, 1.0f); }

	[[nodiscard]] constexpr auto smooth(float t) noexcept -> float
	{
		t = clamp01(t);
		return t * t * (3.0f - 2.0f * t);
	}

	/// 帧率无关的指数逼近：`rate` 越大追得越紧（每秒消去 1 - e^-rate 的差距）。
	[[nodiscard]] inline auto approach(float current, float target, float rate, float seconds) noexcept -> float
	{
		return target + (current - target) * ::std::exp(-rate * seconds);
	}

	/// 本帧时长（秒）。封顶，免得断点 / 卡顿之后动画一下子跳到底。
	[[nodiscard]] inline auto seconds(frame_clock::frame_time const& time) noexcept -> float
	{
		return (::std::min)(::std::chrono::duration<float>(time.delta).count(), 0.1f);
	}

	[[nodiscard]] inline auto viewport_of(renderer const& draw) noexcept -> point
	{
		auto const extent = draw.vulkan.global_env().swapchain_extent();
		return { static_cast<float>(extent.width), static_cast<float>(extent.height) };
	}

	/// 鼠标在窗口坐标里；交换链是可绘制像素。两者在没有 HiDPI 缩放时相等——这里按相等处理。
	[[nodiscard]] inline auto mouse_of(input::state const& current) noexcept -> point
	{
		return { current.mouse.position.x, current.mouse.position.y };
	}

	/// 一根轴的按键重复：按下立刻动一格，按住 350ms 后每 120ms 动一格。
	struct key_repeat
	{
		using clock_type = frame_clock::clock_type;

		clock_type::duration initial = ::std::chrono::milliseconds{ 350 };
		clock_type::duration interval = ::std::chrono::milliseconds{ 120 };
		int _held = 0;
		clock_type::time_point _next{};

		/// `negative` / `positive`：本帧的按下边沿；`held`：当前按住的方向（-1 / 0 / 1）。返回本帧要动的方向。
		auto step(clock_type::time_point now, bool negative, bool positive, int held) noexcept -> int
		{
			auto direction = 0;
			if (negative != positive)
			{
				direction = positive ? 1 : -1;
				_next = now + initial;
			}
			else if (held != _held)
				_next = now + initial;
			else if (held != 0 && now >= _next)
			{
				direction = held;
				_next = now + interval;
			}
			_held = held;
			return direction;
		}

		/// 换了行之后重新计时，避免刚选中就连发。
		void rearm(clock_type::time_point now) noexcept { _next = now + initial; }
	};

	/// 场景的淡入 / 淡出。`overlay()` 是盖在最上面那层黑的透明度。
	struct fade
	{
		float in_duration = 0.45f;
		float out_duration = 0.28f;
		float _elapsed = 0.0f;
		bool _leaving = false;
		bool _done = false;

		[[nodiscard]] auto leaving() const noexcept -> bool { return _leaving; }

		void leave() noexcept
		{
			if (_leaving)
				return;
			_leaving = true;
			_elapsed = 0.0f;
		}

		void reset() noexcept
		{
			_elapsed = 0.0f;
			_leaving = false;
			_done = false;
		}

		/// 推进；淡出在本帧走完时返回 true（只返回一次）。
		auto advance(float dt) noexcept -> bool
		{
			_elapsed += dt;
			if (_leaving && !_done && _elapsed >= out_duration)
			{
				_done = true;
				return true;
			}
			return false;
		}

		[[nodiscard]] auto overlay() const noexcept -> float
		{
			return _leaving ? smooth(_elapsed / out_duration) : 1.0f - smooth(_elapsed / in_duration);
		}
	};

	/// 居中的一列行。update 用它做鼠标命中，record 用它摆位置，两边必须算出同一份。
	struct menu_layout
	{
		point viewport{};
		float scale = 1.0f;
		float width = 0.0f;
		float row_height = 0.0f;
		float gap = 0.0f;
		float left = 0.0f;
		float top = 0.0f;
		::std::size_t rows = 0;

		[[nodiscard]] static auto compute(point viewport, ::std::size_t rows, float design_width) noexcept -> menu_layout
		{
			auto const count = static_cast<float>(rows);
			auto result = menu_layout{ .viewport = viewport, .rows = rows };
			result.scale = (::std::min)({ viewport.x / 960.0f, viewport.y / 540.0f, viewport.y / (count * 60.0f + 210.0f) });
			result.width = design_width * result.scale;
			result.row_height = 48.0f * result.scale;
			result.gap = 12.0f * result.scale;
			result.left = (viewport.x - result.width) * 0.5f;
			result.top = (viewport.y - result.total()) * 0.5f;
			return result;
		}

		[[nodiscard]] auto total() const noexcept -> float
		{
			return static_cast<float>(rows) * row_height + (static_cast<float>(rows) - 1.0f) * gap;
		}

		[[nodiscard]] auto bottom() const noexcept -> float { return top + total(); }

		/// 第 `index` 行；允许小数，给缓动中的光标用。
		[[nodiscard]] auto row(float index) const noexcept -> rect
		{
			return { left, top + index * (row_height + gap), width, row_height };
		}

		[[nodiscard]] auto row(::std::size_t index) const noexcept -> rect { return row(static_cast<float>(index)); }

		[[nodiscard]] auto panel(float above, float below) const noexcept -> rect
		{
			return { left - 28.0f * scale, top - above, width + 56.0f * scale, total() + above + below };
		}

		[[nodiscard]] auto full() const noexcept -> rect { return { 0.0f, 0.0f, viewport.x, viewport.y }; }

		[[nodiscard]] auto hit(point p) const noexcept -> ::std::optional<::std::size_t>
		{
			for (auto index = ::std::size_t{}; index < rows; ++index)
			{
				if (row(index).contains(p))
					return index;
			}
			return ::std::nullopt;
		}
	};

	/// 开一个 secondary 命令缓冲，设好视口 / 裁剪，等着往里画。
	[[nodiscard]] inline auto begin_secondary(renderer& draw, ::vkkl::command_pool const& pool) -> ::vkkl::command_buffer
	{
		using namespace ::vkfu::enums;
		auto const global = draw.vulkan.global_env();
		auto command = ::vkkl::command_buffer{ global.device(), pool.handle, VK_NULL_HANDLE };
		::vkfu::allocate_command_buffers(global.device(), ::vkfu::param::command_buffer{ .command_pool = pool.handle, .level = command_buffer_level::secondary, .command_buffer_count = 1 }, ::std::span{ &command.handle, 1u });
		::vkfu::begin_command_buffer(command.handle, ::vkfu::param::command_buffer_begin{
			.flags = {.one_time_submit = 1, .render_pass_continue = 1},
			.inheritance_info =
				::vkfu::param::command_buffer_inheritance{}
				| ::vkfu::param::option::command_buffer_inheritance_rendering{
					.color_attachment_formats = ::std::array{ global.swapchain_image_format() },
					.rasterization_samples = sample_count::count_1,
				}
			});
		auto const extent = global.swapchain_extent();
		::vkfu::cmd_set_viewport(command.handle, 0, ::std::array{ ::vkfu::evaluate(::vkfu::param::viewport{.width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .max_depth = 1.0f }) });
		::vkfu::cmd_set_scissor(command.handle, 0, ::std::array{ ::vkfu::evaluate(::vkfu::param::rect_2d{.extent = extent }) });
		return command;
	}

	/// 往一个 secondary 里画矩形和文字。坐标是屏幕像素，原点左上。
	struct painter
	{
		VkDevice device;
		VkPhysicalDevice physical_device;
		VkCommandBuffer command;
		vector_graph_type const& vector;
		text_type const& text;
		point viewport;
		::glm::mat3 transform;   ///< 像素 → NDC
		::std::vector<buffer_type>& buffers;

		painter(renderer const& draw, VkCommandBuffer command, vector_graph_type const& vector, text_type const& text, ::std::vector<buffer_type>& buffers) noexcept
			: device(draw.vulkan.global_env().device())
			, physical_device(draw.vulkan.global_env().physical_device())
			, command(command)
			, vector(vector)
			, text(text)
			, viewport(viewport_of(draw))
			, transform(::glm::scale(::glm::translate(::glm::mat3{ 1.0f }, ::glm::vec2{ -1.0f, -1.0f }), ::glm::vec2{ 2.0f / viewport.x, 2.0f / viewport.y }))
			, buffers(buffers)
		{}

		void fill(rect r, color c)
		{
			if (c[3] <= 0.001f || r.width <= 0.0f || r.height <= 0.0f)
				return;
			buffers.emplace_back(vector.draw_rectangle(device, physical_device, command, { r.width, r.height }, c, ::glm::translate(transform, ::glm::vec2{ r.left, r.top })));
		}

		void outline(rect r, float thickness, color c)
		{
			fill({ r.left, r.top, r.width, thickness }, c);
			fill({ r.left, r.bottom() - thickness, r.width, thickness }, c);
			fill({ r.left, r.top + thickness, thickness, r.height - 2.0f * thickness }, c);
			fill({ r.right() - thickness, r.top + thickness, thickness, r.height - 2.0f * thickness }, c);
		}

		/// `anchor` 是文字竖直中心所在的点；`align` -1 左对齐 / 0 居中 / 1 右对齐。
		void label(::std::string_view s, float size, point anchor, int align = 0, color tint = palette::paper, float available_width = 1.0e9f)
		{
			if (s.empty() || tint[3] <= 0.001f)
				return;
			auto const measured = text.measure(s, size, available_width);
			auto const x = anchor.x + static_cast<float>(align) * measured.x * 0.5f;
			buffers.emplace_back(text.label(device, physical_device, command, s, size, available_width, ::glm::translate(transform, ::glm::vec2{ x, anchor.y }), tint));
		}

		[[nodiscard]] auto measure(::std::string_view s, float size, float available_width = 1.0e9f) const -> point
		{
			return text.measure(s, size, available_width);
		}
	};
}
