#pragma once

/// for_each_value<Array>(fn)：对编译期数组的每个元素依次调用 fn.template operator()<Array[i]>()，按下标顺序。
/// 融合 op-state 用它展开"通知每个后继""启动每个根"，降解用它按拓扑序构造节点。

#include <cstddef>
#include <utility>

#include "../config.h"

namespace sdag::meta
{

inline namespace SDAG_ABI_NAMESPACE
{

template<auto Array, class Fn>
constexpr void for_each_value(Fn&& fn)
{
#if SDAG_REFLECTION
	template for (constexpr auto value : Array)
	{
		fn.template operator()<value>();
	}
#else
	[&]<::std::size_t... I>(::std::index_sequence<I...>)
	{
		(fn.template operator()<Array[I]>(), ...);
	}(::std::make_index_sequence<Array.size()>{});
#endif
}

}

}
