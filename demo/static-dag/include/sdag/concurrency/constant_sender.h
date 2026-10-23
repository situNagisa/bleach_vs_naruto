#pragma once

/// constant_sender<Sender>：忽略参数、交出一个事先存好的 sender 的可调用对象。
///
/// 给 let_value 用：let_value(input, constant_sender{payload}) 等 input 完成后丢掉它的值，再启动 payload。
/// sdag 降解只有控制依赖的节点时就是这样拼的（execution/lowering.h），编译期分析也要知道这个类型（graph/analyze/lowered.h）。

#include <type_traits>
#include <utility>

namespace sdag::concurrency
{

template<class Sender>
struct constant_sender
{
	Sender _sender;

	template<class... Args>
	[[nodiscard]] constexpr Sender operator()(Args&&...) const& noexcept(::std::is_nothrow_copy_constructible_v<Sender>)
	{
		return _sender;
	}

	template<class... Args>
	[[nodiscard]] constexpr Sender operator()(Args&&...) && noexcept(::std::is_nothrow_move_constructible_v<Sender>)
	{
		return ::std::move(_sender);
	}
};

}
