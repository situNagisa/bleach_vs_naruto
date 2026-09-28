#pragma once

#include <cstdint>

#include <stdexec/execution.hpp>

#include "./image_state.h"
#include "./mock_vulkan.h"
#include "./tracked_value.h"

/// acquire/present：Vulkan 专属，不需要走 tracked_resource/pending_resource 这两个
/// 通用 concept 的间接层——它们直接使用具体的 pending_value<swapchain_image, State>
/// 类型，present 尤其需要按 State 是不是 present_src 做编译期检查（漏了一次
/// transition，这里就会是类型不匹配，编译不过），这跟"任何满足 tracked_resource
/// 的东西都能用"这种通用性没有关系，是这两个具体算法自己的契约。
namespace gpu
{
/// vkAcquireNextImage2KHR 是一次同步的 CPU 调用：调用返回时 image 就已知，这里
/// 用 just|then 就够，不需要自定义算子状态。fence 参数永远不出现——"CPU 知道
/// acquire 完成"这件事就是这次调用同步返回本身；"image 内容真正 ready"是 GPU 事件，
/// 交给下面 pending_value 里的信号，由 consume()（也就是 consume_external）在
/// 该等的地方去等，不需要在 acquire 这一步额外表达。
[[nodiscard]] inline auto acquire(mock::swapchain& sc)
{
	return ::stdexec::just() | ::stdexec::then([&sc]
	{
		auto const result = mock::acquire_next_image(sc);
		return mock::pending_value<mock::image_handle, undefined>{result.image, undefined{}, result.image_available};
	});
}

/// present 只接受"已经转到 present_src 状态"的 pending 值——这是它的契约，不是
/// tracked_resource/pending_resource 要求的，是这个具体算法自己声明的类型约束。
/// 忘了在它之前写 transition<present_src>()，上游产出的是别的 State，这里的
/// then 直接因为参数类型不匹配而编译失败。
[[nodiscard]] inline auto present(mock::swapchain& sc)
{
	return ::stdexec::then([&sc](mock::pending_value<mock::image_handle, present_src> const& p)
	{
		mock::queue_present(sc, p._handle, p._signal);
	});
}
}
