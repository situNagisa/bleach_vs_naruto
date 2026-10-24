#pragma once

/// 节点运行环境：节点看到的环境 = 取值源句柄 + 可选停止令牌 + 外层转发。依赖：不可移动元组、取值源协议。

#include <concepts>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "../general/tuple.h"
#include "value_source.h"

namespace sdag::protocol
{

/// 节点接收者的环境：取值源指针 +（需要时）整图的停止令牌 + 外层环境的全部转发查询。
///
/// Stoppable 为假时没有停止令牌成员（0 字节），get_stop_token 也转发给外层：图里没有节点会失败，
/// 整图就不需要自己的停止源，节点直接看外层的令牌即可。
///
/// 图嵌套图时，内层节点的 node_env 包着外层节点的 node_env：内层的 get_value_source 遮蔽外层的，
/// 其余查询（包括外层图的停止令牌）逐层转发。
///
/// 取值源是 SourceOf<Key, Env>，以模板模板形参传进来，而不是直接把取值源类型作模板实参：
/// 节点签名是在这个环境下算的，那时取值源（整图状态，其布局依赖节点签名）还没完成；stdexec 检查环境时会做 ADL
/// （例如 __awaitable 查 operator co_await），ADL 会实例化模板类型实参的关联类——取值源若是类型实参就被提前实例化，成环。
/// 模板模板实参不是关联类，不会被实例化。
template<template<class, class> class SourceOf, class Key, class Env, bool Stoppable>
struct node_env
{
	using source_type = SourceOf<Key, Env>;

	source_type* _source;
	SDAG_NO_UNIQUE_ADDRESS ::std::conditional_t<Stoppable, ::stdexec::inplace_stop_token, general::empty_for<node_env>> _stop_token;
	Env _outer;

	[[nodiscard]] constexpr source_type* query(get_value_source_t) const noexcept
	{
		return _source;
	}

	[[nodiscard]] constexpr ::stdexec::inplace_stop_token query(::stdexec::get_stop_token_t) const noexcept
		requires Stoppable
	{
		return _stop_token;
	}

	template<class Query, class... Args>
		requires (!::std::same_as<Query, get_value_source_t>)
			&& (!(Stoppable && ::std::same_as<Query, ::stdexec::get_stop_token_t>))
			&& (::stdexec::forwarding_query(Query{}))
			&& requires(Env const& outer, Args&&... args) { outer.query(Query{}, ::std::forward<Args>(args)...); }
	[[nodiscard]] constexpr decltype(auto) query(Query query, Args&&... args) const
		noexcept(noexcept(_outer.query(query, ::std::forward<Args>(args)...)))
	{
		return _outer.query(query, ::std::forward<Args>(args)...);
	}
};

}
