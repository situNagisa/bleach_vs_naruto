#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include <stdexec/execution.hpp>

#include "../../framework/any_sender.h"
#include "../../framework/frame_slot.h"
#include "../frame_clock.h"
#include "../input.h"
#include "../render.h"

#include "./entity_registry.h"

/// fighting_scene 里的 entity：player、物理、特效……都是它。跟 main_menu 同级——
/// 拿着全局资源（renderer、input）自己干活、自己画；嵌在场景里只是为了让场景替它们
/// 管加载 / 卸载 / 身份。

/// spawn 时给的身份和全局资源视角。指针由场景保证覆盖 entity 的整个生命期。
struct entity_context
{
	::std::uint32_t instance;
	renderer* draw;
	input* controls;
};

/// 本帧只读的视图。成员指向 main 的 let_value 里存的值和场景的 registry，覆盖整帧。
struct frame_view
{
	frame_clock::frame_time const* time;
	input::state const* current;
	input::delta const* changes;
	entity_registry const* registry;
};

enum class phase : ::std::uint8_t
{
	update = 1 << 0,
	settle = 1 << 1,
	record = 1 << 2,
};

[[nodiscard]] constexpr auto operator|(phase left, phase right) noexcept -> phase
{
	return static_cast<phase>(static_cast<::std::uint8_t>(left) | static_cast<::std::uint8_t>(right));
}

[[nodiscard]] constexpr auto has(phase set, phase one) noexcept -> bool
{
	return (static_cast<::std::uint8_t>(set) & static_cast<::std::uint8_t>(one)) != 0;
}

/// 一帧四步，每步的读写规则就是这个框架的全部并发约定：
///
/// | 步 | 怎么跑 | 能读 | 能写 |
/// | --- | --- | --- | --- |
/// | publish | 同步，按 instance 序 | 自己 | 自己的共享对象、registry |
/// | update | 所有参与者并发 | 自己、别人的共享对象（只读） | 自己的私有状态、别人的 `mutable` 通道 |
/// | settle | 所有参与者并发 | 只有自己 | 只有自己（含自己的共享对象和 inbox） |
/// | record | 所有参与者并发 | 只有自己 | 自己的绘制资源、`out` |
///
/// 步与步之间是 when_all 屏障。不参与某一步就别在 `phases()` 里报，场景不会为它连 sender。
///
/// **返回的 sender 析构时不得访问 entity**：despawn 发生在帧末，那时本帧的 operation_state
/// 可能还没析构（sync_wait 返回才析构）。dll 不会在那之前卸载，见 module_cache::collect。
struct fighting_entity
{
	virtual ~fighting_entity() = default;

	[[nodiscard]] virtual auto phases() const noexcept -> phase = 0;

	virtual void publish(entity_registry& registry) = 0;

	[[nodiscard]] virtual auto update(frame_view const&) -> values_sender_type<> { return ::stdexec::just(); }
	[[nodiscard]] virtual auto settle(frame_view const&) -> values_sender_type<> { return ::stdexec::just(); }

	/// 录一个 secondary 写进 `out`（不画就留 VK_NULL_HANDLE）。命令缓冲和顶点缓冲由 entity
	/// 自己跨帧持有，下一帧录制时再覆盖：上一帧的 fence 在 renderer::end 里已经等过。
	[[nodiscard]] virtual auto record(frame_slot&, ::VkCommandBuffer& out) -> values_sender_type<> { out = VK_NULL_HANDLE; return ::stdexec::just(); }
};
