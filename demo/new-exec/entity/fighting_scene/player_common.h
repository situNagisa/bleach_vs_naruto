#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm/glm.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

#include <vkkl/command_buffer.h>
#include <vkkl/command_pool.h>

#include "../input.h"
#include "../render.h"
#include "../main_menu/buffer_type.h"
#include "../main_menu/vector_graph.h"

/// 格斗场景里所有 player 共用的纯数据和小部件。宿主和插件都 include，所以全部 header-only。

struct player_snapshot
{
	float x = 0.0f;
	float facing = 1.0f;
	int hp = 100;
};

struct hit_event
{
	::std::uint32_t attacker;
	int damage;
	float knockback;
};

/// 本帧输入。指向 poll 的完成值，它们活在场景 `let_value` 的状态里，覆盖整个阶段。
struct frame_input
{
	input::state const* current;
	input::delta const* changes;
};

/// record 阶段的扇入：每个 player 按 instance 写自己的槽，**互不加锁**。
/// 存储归场景，只在帧边界随 instance 数扩容。renderer 只看 `commands()`，不知道有几个 player。
struct command_list
{
	::std::span<::VkCommandBuffer> _slots{};
	::std::size_t _count = 0;

	void reset() noexcept
	{
		::std::ranges::fill(_slots, VK_NULL_HANDLE);
		_count = 0;
	}

	void put(::std::uint32_t instance, ::VkCommandBuffer command) noexcept
	{
		assert(instance < _slots.size() && _slots[instance] == VK_NULL_HANDLE);
		_slots[instance] = command;
	}

	/// 阶段结束后调用一次：把本帧没画的空槽挤掉。按 instance 序 = 绘制序。
	void compact() noexcept
	{
		auto const kept = ::std::ranges::remove(_slots, VK_NULL_HANDLE);
		_count = static_cast<::std::size_t>(kept.begin() - _slots.begin());
	}

	[[nodiscard]] auto commands() const noexcept -> ::std::span<::VkCommandBuffer const>
	{
		return _slots.first(_count);
	}
};

/// 一个 player 自己的绘制资源：自己的命令池、自己录 secondary，直接用 renderer 的设备。
///
/// `_command` / `_buffers` 跨帧持有，下一帧录制时才覆盖：上一帧的 fence 在它的
/// `renderer::end` 里已经等过，覆盖时 GPU 一定不再用它们。
struct player_drawing
{
	renderer* _renderer;
	::vkkl::command_pool _pool;
	vector_graph_type _vector_graph;
	::vkkl::command_buffer _command{};
	::std::vector<buffer_type> _buffers{};

	explicit player_drawing(renderer& draw)
		: _renderer(&draw)
		, _pool(::consumer_arch_vulkan::create_secondary_command_pool(draw.vulkan.global_env()))
		, _vector_graph(draw.vulkan.global_env().device(), draw.vulkan.global_env().swapchain_image_format())
	{}

	/// 开一个 secondary，交给 `draw(device, physical_device, command, viewport_transform, unit, ground)` 往里画。
	template <class Draw>
	auto record(Draw&& draw) -> ::VkCommandBuffer
	{
		using namespace ::vkfu::enums;
		auto const global = _renderer->vulkan.global_env();
		auto const device = global.device();
		auto const extent = global.swapchain_extent();

		_buffers.clear();
		_command = ::vkkl::command_buffer{ device, _pool.handle, VK_NULL_HANDLE };
		::vkfu::allocate_command_buffers(device, ::vkfu::param::command_buffer{ .command_pool = _pool.handle, .level = command_buffer_level::secondary, .command_buffer_count = 1 }, ::std::span{ &_command.handle, 1u });
		::vkfu::begin_command_buffer(_command.handle, ::vkfu::param::command_buffer_begin{
			.flags = {.one_time_submit = 1, .render_pass_continue = 1},
			.inheritance_info =
				::vkfu::param::command_buffer_inheritance{}
				| ::vkfu::param::option::command_buffer_inheritance_rendering{
					.color_attachment_formats = ::std::array{ global.swapchain_image_format() },
					.rasterization_samples = sample_count::count_1,
				}
			});
		::vkfu::cmd_set_viewport(_command.handle, 0, ::std::array{ ::vkfu::evaluate(::vkfu::param::viewport{.width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .max_depth = 1.0f }) });
		::vkfu::cmd_set_scissor(_command.handle, 0, ::std::array{ ::vkfu::evaluate(::vkfu::param::rect_2d{.extent = extent }) });

		auto const viewport = point(extent.width, extent.height);
		auto const viewport_transform = ::glm::scale(
			::glm::translate(::glm::mat3{ 1.0f }, ::glm::vec2{ -1.0f, -1.0f }),
			::glm::vec2{ 2.0f / viewport.x, 2.0f / viewport.y });

		::std::forward<Draw>(draw)(viewport, viewport_transform);

		::consumer_arch_vulkan::check(::vkEndCommandBuffer(_command.handle), "player: end draw");
		return _command.handle;
	}

	/// 世界坐标 x ∈ [-5, 5] 映射到屏幕宽度；y 向上，0 是地面。
	void rectangle(point viewport, ::glm::mat3 const& viewport_transform, float x, float y, point size, color fill)
	{
		auto const global = _renderer->vulkan.global_env();
		auto const unit = viewport.x / 10.0f;
		auto const ground = viewport.y * 0.8f;
		auto const pixels = point{ size.x * unit, size.y * unit };
		auto const left = (x + 5.0f) * unit - pixels.x * 0.5f;
		auto const top = ground - y * unit - pixels.y;
		_buffers.emplace_back(_vector_graph.draw_rectangle(global.device(), global.physical_device(), _command.handle,
			pixels, fill, ::glm::translate(viewport_transform, ::glm::vec2{ left, top })));
	}

	/// 本体 + 血条。
	void body(point viewport, ::glm::mat3 const& viewport_transform, player_snapshot const& snapshot, color fill)
	{
		rectangle(viewport, viewport_transform, snapshot.x, 0.0f, { 0.6f, 1.2f }, fill);
		auto const hp = static_cast<float>((::std::max)(snapshot.hp, 0)) / 100.0f;
		rectangle(viewport, viewport_transform, snapshot.x - 0.3f * (1.0f - hp), 1.35f, { 0.6f * hp, 0.08f }, { 0.9f, 0.2f, 0.2f, 1.0f });
	}
};
