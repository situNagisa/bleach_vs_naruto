#pragma once

/// 图 sender 对外报告的东西：完成签名、属性（完成域、完成行为），以及"要不要降解"的判据。
///
/// 完成签名
///   与 when_all 同一套规则：值完成是各汇点的值按声明顺序拼接（只有一个汇点时就是它的原始签名）；
///   错误是各节点错误类型的并集，值存进 op-state 可能抛异常时再加 exception_ptr；任一节点会 stopped 时有 set_stopped。
///   签名要在节点实际看到的环境（node_env_t）下算——节点的签名可能依赖环境里的东西（调度器、停止令牌……）。
///
/// 属性
///   值完成发生在最后一个汇点完成的地方：取各汇点的公共完成域。
///   错误 / 停止可能来自任一节点：取汇点与全部节点的公共域。完成行为取全部节点的合成。
///
/// 降解判据
///   stdexec 在 connect 之前会调用 transform_sender，分两个阶段问 domain：start_t 阶段问环境里的起始 domain，
///   set_value_t 阶段问 sender 自己报告的完成域。我们想知道的是："如果把这张图写成 when_all / split / let_value，
///   这个 domain 会不会改写它"。会（例如 GPU 的 stream_domain），就降解，让 domain 拿到它认识的标准算法；
///   不会（默认 domain、只定制 bulk 的线程池 domain），就保持融合，0 开销。
///   直接问"会不会改写降解后的整棵树"代价太大，所以拿一个探针问：when_all(各汇点)，其中汇点里的输入叶子换成
///   签名相同的占位 sender。domain 对 when_all 的定制看的正是这一层。

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/completion_behavior.hpp>

#include "meta.h"
#include "fwd.h"
#include "stdexec_bridge.h"
#include "graph_traits.h"
#include "node_input.h"
#include "compute/primitives.h"
#include "compute/analysis.h"

namespace sdag::graph_detail
{

// ---------------------------------------------------------------- 完成签名

/// 有接收者环境时：先在探测环境、再在实际节点环境下检查每个节点；有不合法的就把 stdexec 给出的错误类型原样返回，
/// 让 stdexec 的诊断指向真正出错的节点 sender，而不是 sdag 内部。
template<class Self, class Env>
[[nodiscard]] consteval auto completions_with_env()
{
	using probe = compute::analysis<Self, probe_env<Self, Env>>;
	if constexpr (!probe::valid)
	{
		return typename probe::first_invalid_t{};
	}
	else
	{
		using final = compute::analysis<Self, node_env_t<Self, Env>>;
		if constexpr (!final::valid)
		{
			return typename final::first_invalid_t{};
		}
		else
		{
			return typename final::completions{};
		}
	}
}

template<class Self, class... Env>
[[nodiscard]] consteval auto completions_of()
{
	if constexpr (sizeof...(Env) == 0)
	{
		// 节点里的输入 sender 依赖环境，所以整图在无环境下必然是 dependent sender。
		return bridge::dependent_sender_error<::std::remove_cvref_t<Self>>();
	}
	else
	{
		return graph_detail::completions_with_env<Self, Env...>();
	}
}

/// 节点签名在 Env 下都合法。属性查询与降解判据先问这个：不合法时安静地不回答，把诊断留给 connect。
template<class Self, class Env>
[[nodiscard]] consteval bool evaluable_in() noexcept
{
	if constexpr (!compute::analysis<Self, probe_env<Self, Env>>::valid)
	{
		return false;
	}
	else
	{
		return compute::analysis<Self, node_env_t<Self, Env>>::valid;
	}
}

// ---------------------------------------------------------------- 属性

template<class Data, class... Children>
struct attrs
{
	using self_type = graph_sender<Data, Children...>&&;

	template<class Env, class Indices = compute::sequence_t<sinks_v<Data::shape>>>
	struct sink_children;

	template<class Env, size_t... S>
	struct sink_children<Env, ::std::index_sequence<S...>>
	{
		using type = meta::type_list<Children...[S]...>;
	};

	/// 汇点在节点环境下的公共值完成域；有汇点给不出时为 void。
	template<class Env>
	using value_domain_t = compute::common_completion_domain_t<node_env_t<self_type, Env>, typename sink_children<Env>::type>;

	/// 全部节点的公共值完成域；有节点给不出时为 void。
	template<class Env>
	using any_node_domain_t = compute::common_completion_domain_t<node_env_t<self_type, Env>, meta::type_list<Children...>>;

	template<class Env>
	using failure_domain_t = compute::common_domain_t<meta::type_list<value_domain_t<Env>, any_node_domain_t<Env>>>;

	template<class Env>
		requires (evaluable_in<self_type, Env>()) && (!::std::is_void_v<value_domain_t<Env>>)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>, Env const&) const noexcept
		-> value_domain_t<Env>
	{
		return {};
	}

	template<class Env>
		requires (evaluable_in<self_type, Env>()) && (!::std::is_void_v<value_domain_t<Env>>) && (!::std::is_void_v<any_node_domain_t<Env>>)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_error_t>, Env const&) const noexcept
		-> failure_domain_t<Env>
	{
		return {};
	}

	template<class Env>
		requires (evaluable_in<self_type, Env>()) && (!::std::is_void_v<value_domain_t<Env>>) && (!::std::is_void_v<any_node_domain_t<Env>>)
	[[nodiscard]] constexpr auto query(::stdexec::get_completion_domain_t<::stdexec::set_stopped_t>, Env const&) const noexcept
		-> failure_domain_t<Env>
	{
		return {};
	}

	/// 子 sender 的完成行为：直接调公开的查询对象，结果是类型化的常量（exec::completion_behavior::*_t）。
	template<class Tag, class Child, class... Env>
	using child_behavior_t = decltype(::exec::get_completion_behavior_t<Tag>{}(
		::stdexec::get_env(::std::declval<Child const&>()), ::std::declval<Env const&>()...));

	template<class Tag, class... Env>
	[[nodiscard]] constexpr auto query(::exec::get_completion_behavior_t<Tag>, Env const&...) const noexcept
	{
		return ::exec::completion_behavior::common(child_behavior_t<Tag, Children, Env...>{}...);
	}
};

/// 空图：start 里立即 set_value()。
template<class Data>
struct attrs<Data>
{
	template<class Tag>
	[[nodiscard]] constexpr auto query(::exec::get_completion_behavior_t<Tag>) const noexcept
	{
		return ::exec::completion_behavior::inline_completion;
	}
};

// ---------------------------------------------------------------- 降解判据

/// 探针里的占位 sender：完成签名与节点真实的输入 sender 在整图环境下相同，属性也相同。只用来算类型，不会被连接。
template<class Completions>
struct placeholder_sender
{
	using sender_concept = ::stdexec::sender_t;

	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures() noexcept
	{
		return Completions{};
	}

	[[nodiscard]] static constexpr input_attrs get_env() noexcept
	{
		return {};
	}
};

/// 汇点 S 的 sender，输入叶子换成占位 sender：保留了汇点的属性（完成调度器等），又不需要整图环境就能算签名。
template<class Self, class Env, size_t S>
using probe_sink_t = compute::substituted_t<child_t<Self, S>,
	placeholder_sender<compute::input_completions_t<Self, dependency_t<Self, S>, node_env_t<Self, Env>>>>;

template<class Self, class Env, class Sinks = compute::sequence_t<sinks_v<shape_of_v<Self>>>>
struct when_all_probe;

/// 探针 when_all(汇点...)。when_all 只有一个子 sender 时会原样返回它（不再是 when_all），所以只有一个汇点时重复一次。
template<class Self, class Env, size_t... S>
struct when_all_probe<Self, Env, ::std::index_sequence<S...>>
{
	static consteval auto compute() noexcept
	{
		if constexpr (sizeof...(S) == 1)
		{
			return ::std::type_identity<decltype(::stdexec::when_all(::std::declval<probe_sink_t<Self, Env, S>>()...,
				::std::declval<probe_sink_t<Self, Env, S>>()...))>{};
		}
		else
		{
			return ::std::type_identity<decltype(::stdexec::when_all(::std::declval<probe_sink_t<Self, Env, S>>()...))>{};
		}
	}

	using type = typename decltype(compute())::type;
};

/// transform_sender 这一阶段问的是哪个 domain。规则与 stdexec 相同：
/// start_t 阶段用 get_domain(env)；set_value_t 阶段用 get_completion_domain<>（先问 void 版本，再回落到 set_value_t）。
template<class OpTag, class Self, class Env>
[[nodiscard]] consteval auto phase_domain() noexcept
{
	if constexpr (::std::same_as<OpTag, ::stdexec::start_t>)
	{
		return ::std::type_identity<decltype(::stdexec::get_domain(::std::declval<Env const&>()))>{};
	}
	else if constexpr (requires { ::stdexec::get_completion_domain<>(::stdexec::get_env(::std::declval<Self>()), ::std::declval<Env const&>()); })
	{
		return ::std::type_identity<decltype(::stdexec::get_completion_domain<>(
			::stdexec::get_env(::std::declval<Self>()), ::std::declval<Env const&>()))>{};
	}
	else
	{
		return ::std::type_identity<::stdexec::default_domain>{};
	}
}

/// Domain 会不会改写 Sender：它有能接受这个 sender 的 transform_sender，并且结果与 default_domain 的不同。
template<class Domain, class OpTag, class Sender, class Env>
concept customizes = requires { ::std::declval<Domain>().transform_sender(OpTag{}, ::std::declval<Sender>(), ::std::declval<Env const&>()); }
	&& !::std::same_as<
		decltype(::std::declval<Domain>().transform_sender(OpTag{}, ::std::declval<Sender>(), ::std::declval<Env const&>())),
		decltype(::stdexec::default_domain{}.transform_sender(OpTag{}, ::std::declval<Sender>(), ::std::declval<Env const&>()))>;

/// 这一阶段要不要把图降解成 when_all / split / let_value。
template<class OpTag, class Self, class Env>
[[nodiscard]] consteval bool lowering_wanted() noexcept
{
	if constexpr (!graph_detail::evaluable_in<Self, Env>())
	{
		return false;
	}
	else
	{
		using domain = typename decltype(graph_detail::phase_domain<OpTag, Self, Env>())::type;
		if constexpr (::std::same_as<domain, ::stdexec::default_domain>)
		{
			return false;
		}
		else
		{
			return customizes<domain, OpTag, typename when_all_probe<Self, Env>::type, Env>;
		}
	}
}

}
