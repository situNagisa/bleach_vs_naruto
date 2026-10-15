#pragma once

#include <array>
#include <span>
#include <utility>

#include <stdexec/execution.hpp>

#include <nagisa/concurrency/lease.h>

#include "../framework/frame_slot.h"

#include "./detail/demo_vulkan.h"

/// renderer 只提供一帧的骨架：acquire/begin → 调用方录制 → submit/present → 等 fence。
///
/// 录制什么、有几个录制者、命令缓冲放在哪，全归调用方的 recording；renderer 只要求
/// `recording.commands()` 给出一段 `VkCommandBuffer`。recording 存在 `let_value` 的
/// operation_state 里，天然活到 fence 之后，所以不需要 split、不需要 recorders 容器、
/// 不需要往 roots 里挂"延长生命周期"的节点。
struct renderer
{
	using resource_pool = ::nagisa::concurrency::bounded_lease_pool<frame_slot*, 4>;
	using lease = resource_pool::lease;

	renderer();

	void set_fullscreen(bool enabled);

	/// sender<lease>：拿到空闲帧槽，开始主命令缓冲。
	[[nodiscard]] auto begin()
	{
		return _pool.acquire()
			| ::stdexec::then([this](lease slot) -> lease
				{
					_begin(*slot.token());
					return slot;
				});
	}

	/// sender<>：执行 secondary、提交、呈现、等 fence。
	/// @pre commands 指向的存储活到本 sender 完成。
	[[nodiscard]] auto end(frame_slot& frame, ::std::span<::VkCommandBuffer const> commands)
	{
		return ::stdexec::just()
			| ::stdexec::then([this, &frame, commands]
				{
					_submit_present(frame, commands);
					_wait_fence(frame);
				});
	}

	/// 一帧。
	///
	/// `upstream` 与 `begin()` 并发；两者都完成后调用 `record(frame_slot&, upstream 的值...)`，
	/// 它返回 sender<recording>。lease 和 recording 都存在各自 `let_value` 的状态里，
	/// 生命周期由结构保证覆盖到 fence 完成。
	///
	/// 想要多个录制者，就在 record 里自己 `when_all`，再把结果拼成一个 recording；
	/// renderer 不规定扇入用 tuple、array 还是动态范围。
	template <class Upstream, class Record>
	[[nodiscard]] auto frame(Upstream upstream, Record record)
	{
		return ::stdexec::when_all(begin(), ::std::move(upstream))
			| ::stdexec::let_value([this, record = ::std::move(record)](lease& slot, auto&... values) mutable
				{
					auto& target = *slot.token();
					return record(target, values...)
						| ::stdexec::let_value([this, &target](auto& recording)
							{
								return end(target, recording.commands());
							});
				});
	}

	void _begin(frame_slot& frame);
	void _submit_present(frame_slot const& frame, ::std::span<::VkCommandBuffer const> commands);
	void _wait_fence(frame_slot const& frame);

	::bvn::platform::window window;
	::consumer_arch_vulkan::vulkan_context vulkan;
	::std::array<frame_slot, 4> _frames;
	resource_pool _pool;
};
