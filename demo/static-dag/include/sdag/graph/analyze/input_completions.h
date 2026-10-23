#pragma once

/// input_completions_t<Self, after<...>, Env>：节点输入 sender 在环境 Env 下的完成签名。
///
///   前驱的值按 after<...> 的声明顺序拼接：只有一个读者的值给 V（move 过去），多个读者共享的值给 V const&；
///   某个前驱没有值完成（例如 just_stopped）时是 completion_signatures<>（这个节点永远不会被启动）；
///   某个前驱的签名算不出来时，是那个不合法的结果本身（原样透传给 stdexec 诊断）。
///
/// 只看前驱节点的签名，不碰整图分析：整图分析要算每个节点的签名，节点的签名又要问它的输入 sender——
/// 如果输入 sender 反过来要整图分析，就成环了。

#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../../config.h"
#include "../../meta_program/type_list.h"
#include "../../meta_program/utility.h"
#include "../../meta_program/indexed_tuple.h"
#include "../../graph/traits.h"
#include "../../meta_program/reflection.h"
#include "../../meta_program/concat.h"
#include "../../meta_program/transform.h"
#include "../../meta_program/sequence.h"
#include "../../concurrency/completion_signatures.h"
#include "../../graph/analyze/completions.h"
#include "../../graph/analyze/consumers.h"

namespace sdag::analyze
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace detail
{

using namespace ::sdag::meta::refl;

/// 共享的值按 const& 交给后继；独占的值 move 过去（签名写成按值）。
[[nodiscard]] consteval info access_type(info value, bool shared)
{
	return shared ? ::std::meta::add_lvalue_reference(::std::meta::add_const(value)) : value;
}

template<class Self, class Dependency, class Env>
[[nodiscard]] consteval info input_completions()
{
	infos values;
	bool has_values = true;
	template for (constexpr ::std::size_t from : graph_detail::dependency_indices_v<Self, Dependency>)
	{
		static_assert(from < graph_detail::count_of_v<Self>, "sdag: 输入 sender 引用了图里不存在的键");
		using completions = graph_detail::node_completions_t<Self, from, Env>;
		if constexpr (!concurrency::is_stdexec_completion_signatures_v<completions>)
		{
			return ::std::meta::dealias(^^completions);
		}
		else
		{
			using trait = concurrency::completion_trait<completions>;
			if (!trait::has_values)
			{
				has_values = false;
			}
			for (info value : arguments(^^typename trait::values))
			{
				values.push_back(access_type(value, graph_detail::shared_v<Self, from>));
			}
		}
	}
	if (!has_values)
	{
		return ^^::stdexec::completion_signatures<>;
	}
	return completions_refl::value_completions(list(values));
}

}

template<class Self, class Dependency, class Env>
using input_completions_t = typename [: detail::input_completions<Self, Dependency, Env>() :];

#else

namespace detail
{

using ::std::size_t;

template<bool Shared>
struct accessor
{
	/// 共享的值按 const& 交给后继；独占的值 move 过去（签名写成按值）。
	template<class V>
	using fn = ::std::conditional_t<Shared, V const&, V>;
};

template<class Self, class Env, class Indices>
struct input_completions;

template<class Self, class Env, size_t... D>
struct input_completions<Self, Env, ::std::index_sequence<D...>>
{
	static_assert(((D < graph_detail::count_of_v<Self>) && ...), "sdag: 输入 sender 引用了图里不存在的键");

	template<size_t J>
	using completions_of = graph_detail::node_completions_t<Self, J, Env>;

	template<size_t J>
	using node = concurrency::completion_trait<completions_of<J>>;

	/// 按声明顺序第一个算不出签名的前驱。
	template<size_t... Rest>
	struct first_invalid
	{
		using type = void;
	};

	template<size_t First, size_t... Rest>
	struct first_invalid<First, Rest...>
	{
		using type = ::std::conditional_t<concurrency::is_stdexec_completion_signatures_v<completions_of<First>>,
			typename first_invalid<Rest...>::type,
			completions_of<First>>;
	};

	static consteval auto compute() noexcept
	{
		if constexpr (!(concurrency::is_stdexec_completion_signatures_v<completions_of<D>> && ...))
		{
			return ::std::type_identity<typename first_invalid<D...>::type>{};
		}
		else if constexpr (!(node<D>::has_values && ...))
		{
			return ::std::type_identity<::stdexec::completion_signatures<>>{};
		}
		else
		{
			return ::std::type_identity<value_completions_t<meta::concat_t<
				meta::transform_t<accessor<graph_detail::shared_v<Self, D>>::template fn, typename node<D>::values>...>>>{};
		}
	}

	using type = typename decltype(compute())::type;
};

}

template<class Self, class Dependency, class Env>
using input_completions_t = typename detail::input_completions<Self, Env,
	meta::sequence_t<graph_detail::dependency_indices_v<Self, Dependency>>>::type;

#endif

}

}
