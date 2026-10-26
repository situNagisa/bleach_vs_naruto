#pragma once

/// 图 sender：把 描述 + 完成签名 + 属性 + 执行 拼成一个 stdexec sender；并保证取值源的两个接口都有实现者。
///
///   graph_sender<Tag, Data, Children...>
///     数据与 tuple-like 协议继承自 graph::graph_description（[Tag, data, 节点 sender...]，stdexec 据此认出 tag_of_t）
///     get_env()                     graph::graph_attributes
///     get_completion_signatures()   graph::completions_of
///     connect()                     execution::fused_operation（融合 op-state）
///
/// 取值源：节点环境里放的是 graph::graph_state<Self, Env>——签名接口由 value_source_traits 的特化转到 graph::pred_completions_t，自己实现交付接口。
/// 两个实现者都通过 graph/slots.h 进来，所以只要用到 graph_sender，协议就有完整的实现。

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../graph/description.h"
#include "../graph/attributes.h"
#include "../graph/completions.h"
#include "../graph/pred_sigs.h"
#include "../graph/slots.h"
#include "../execution/fused.h"

namespace sdag
{

inline namespace SDAG_ABI_NAMESPACE
{

template<class Tag, class Data, class... Children>
struct graph_sender : graph::graph_description<Tag, Data, Children...>
{
	using sender_concept = ::stdexec::sender_t;

	[[nodiscard]] constexpr graph::graph_attributes<graph::graph_state, graph_sender&&> get_env() const noexcept
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
			return graph::completions_of<graph::graph_state, graph_sender const&, Env...>();
		}
		else
		{
			return graph::completions_of<graph::graph_state, graph_sender&&, Env...>();
		}
	}

	template<::stdexec::receiver Receiver>
	[[nodiscard]] constexpr execution::fused_operation<graph_sender&&, Receiver> connect(Receiver receiver) &&
		noexcept(::std::is_nothrow_constructible_v<execution::fused_operation<graph_sender&&, Receiver>, graph_sender&&, Receiver>)
	{
		return execution::fused_operation<graph_sender&&, Receiver>(::std::move(*this), ::std::move(receiver));
	}

	template<::stdexec::receiver Receiver>
		requires ::std::copy_constructible<graph_sender>
	[[nodiscard]] constexpr execution::fused_operation<graph_sender const&, Receiver> connect(Receiver receiver) const&
		noexcept(::std::is_nothrow_constructible_v<execution::fused_operation<graph_sender const&, Receiver>, graph_sender const&, Receiver>)
	{
		return execution::fused_operation<graph_sender const&, Receiver>(*this, ::std::move(receiver));
	}
};

}

}

/// tuple-like 协议：图 sender 与它的描述同形。
template<class Tag, class Data, class... Children>
struct std::tuple_size<::sdag::graph_sender<Tag, Data, Children...>>
	: ::std::tuple_size<::sdag::graph::graph_description<Tag, Data, Children...>>
{
};

template<::std::size_t I, class Tag, class Data, class... Children>
struct std::tuple_element<I, ::sdag::graph_sender<Tag, Data, Children...>>
	: ::std::tuple_element<I, ::sdag::graph::graph_description<Tag, Data, Children...>>
{
};
