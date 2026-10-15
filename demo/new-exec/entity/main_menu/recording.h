#pragma once

#include <span>
#include <vector>

#include <vkkl/command_buffer.h>

#include "./buffer_type.h"

/// 一个 secondary 命令缓冲加上它引用的顶点缓冲。
///
/// 它就是这些资源这一帧的宿主：renderer 把它存在 `let_value` 的状态里，
/// fence 完成后才析构。原来靠 split + release root 维持的生命周期，现在由结构保证。
struct recording
{
	::vkkl::command_buffer command{};
	::std::vector<buffer_type> buffers{};

	[[nodiscard]] auto commands() const noexcept -> ::std::span<::VkCommandBuffer const>
	{
		return { &command.handle, 1u };
	}
};
