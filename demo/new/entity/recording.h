#pragma once

#include <span>
#include <vector>

#include <vkkl/command_buffer.h>

#include "./main_menu/buffer_type.h"

/// 一个 secondary 命令缓冲 + 它引用的顶点缓冲。它是这些资源这一帧的**宿主**：
/// main 把 record 的完成值存在 `let_value` 里，fence 等完之后才析构它。
///
/// 只是一个现成部件：每个场景的 `record` 完成值是场景自己的 `recording` 类型，
/// main 只要求它有 `commands()`。
struct secondary_recording
{
	::vkkl::command_buffer command{};
	::std::vector<buffer_type> buffers{};

	[[nodiscard]] auto commands() const noexcept -> ::std::span<::VkCommandBuffer const>
	{
		return { &command.handle, 1u };
	}
};
