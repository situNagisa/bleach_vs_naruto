#pragma once

/// 图 sender 的类型：graph_sender<dag_data<键表, 依赖表>, 节点 sender...>。
///
/// 它按 [dag_t, data, 节点 sender...] 实现标准的 tuple-like 协议（std::tuple_size / std::tuple_element / 成员 get<I>），
/// 和 stdexec 自己的算法 sender 是同一个形状：
///   - stdexec 靠结构化绑定认出它的标签：tag_of_t<G> 是 dag_t，exec::sender_for<G, dag_t> 成立；
///   - domain 可以像拆任何算法 sender 一样拆它（stdexec 的 __structured_apply 也走这个协议）。
/// 这样不需要继承 stdexec 的内部类型 __sexpr。
///
/// 这里只有数据与协议；完成签名、属性在 attributes.h，connect 返回的融合 op-state 在 fused.h。

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "meta.h"
#include "fwd.h"

namespace sdag
{

inline namespace SDAG_ABI_NAMESPACE
{

template<class Data, class... Children>
struct graph_sender
{
	using sender_concept = ::stdexec::sender_t;

	SDAG_NO_UNIQUE_ADDRESS Data _data;
	/// 节点 sender。dag_t 用工厂把它们直接构造在这里，不经过临时对象再移动。
	meta::indexed_tuple<Children...> _children;

	/// tuple-like 协议：[0] 是标签，[1] 是 data，[2...] 是节点 sender。返回的引用跟随 self 的值类别。
	template<::std::size_t I, class Self>
	[[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept
	{
		if constexpr (I == 0)
		{
			// dag_t 在这里还没定义完，经 tuple_element 写成依赖名，实例化时才查。
			return typename ::std::tuple_element<0, ::std::remove_cvref_t<Self>>::type{};
		}
		else if constexpr (I == 1)
		{
			return (::std::forward<Self>(self)._data);
		}
		else
		{
			return meta::get<I - 2>(::std::forward<Self>(self)._children);
		}
	}

	[[nodiscard]] constexpr graph_detail::attrs<Data, Children...> get_env() const noexcept
	{
		return {};
	}

	/// 完成签名与下面两个 connect 一一对应：右值图 move 节点 sender，左值图（无论是否 const）拷贝它们。
	/// 节点 sender 被拷贝还是被移动可能影响它们的签名，所以两种情况分开算。
	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures()
	{
		if constexpr (::std::is_lvalue_reference_v<Self> || ::std::is_const_v<::std::remove_reference_t<Self>>)
		{
			return graph_detail::completions_of<graph_sender const&, Env...>();
		}
		else
		{
			return graph_detail::completions_of<graph_sender&&, Env...>();
		}
	}

	template<::stdexec::receiver Receiver>
	[[nodiscard]] fused_detail::fused_operation<graph_sender&&, Receiver> connect(Receiver receiver) &&
		noexcept(::std::is_nothrow_constructible_v<fused_detail::fused_operation<graph_sender&&, Receiver>, graph_sender&&, Receiver>)
	{
		return fused_detail::fused_operation<graph_sender&&, Receiver>(::std::move(*this), ::std::move(receiver));
	}

	template<::stdexec::receiver Receiver>
		requires ::std::copy_constructible<graph_sender>
	[[nodiscard]] fused_detail::fused_operation<graph_sender const&, Receiver> connect(Receiver receiver) const&
		noexcept(::std::is_nothrow_constructible_v<fused_detail::fused_operation<graph_sender const&, Receiver>, graph_sender const&, Receiver>)
	{
		return fused_detail::fused_operation<graph_sender const&, Receiver>(*this, ::std::move(receiver));
	}
};

}

}

template<class Data, class... Children>
struct std::tuple_size<::sdag::graph_sender<Data, Children...>> : ::std::integral_constant<::std::size_t, 2 + sizeof...(Children)>
{
};

template<class Data, class... Children>
struct std::tuple_element<0, ::sdag::graph_sender<Data, Children...>>
{
	using type = ::sdag::dag_t;
};

template<class Data, class... Children>
struct std::tuple_element<1, ::sdag::graph_sender<Data, Children...>>
{
	using type = Data;
};

template<::std::size_t I, class Data, class... Children>
	requires (I >= 2)
struct std::tuple_element<I, ::sdag::graph_sender<Data, Children...>>
{
	using type = Children...[I - 2];
};
