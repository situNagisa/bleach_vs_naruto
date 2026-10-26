#pragma once

/// 前驱的签名：占位符的签名 = 前驱节点的值签名（共享的给 const&）；它就是取值源的「签名接口」（graph/slots.h）。
///
///   pred_completions_t<Self, data<...>, Env>
///       前驱的值按 data<...> 的声明顺序拼接：take<K> 的给 V（move 过去），其余给 V const&；
///       某个前驱没有值完成（例如 just_stopped）时是 completion_signatures<>（这个节点永远不会被启动）；
///       某个前驱的签名算不出来时，是那个不合法的结果本身（原样透传给 stdexec 诊断）。
///
/// 只看前驱节点的签名，不碰整图分析：整图分析要算每个节点的签名，节点的签名又要问它的占位符——
/// 如果占位符反过来要整图分析，就成环了。

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/sequence.h"
#include "../general/signatures.h"
#include "../protocol/value_source.h"
#include "description.h"
#include "analysis.h"
#include "readers.h"

namespace sdag::graph
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace detail
{

using namespace ::sdag::general::refl;

/// 0, 1, ..., data<...> 的前驱个数 - 1。
template<class Dependency>
inline constexpr auto positions_v = []
{
	::std::array<::std::size_t, Dependency::keys.size()> result{};
	for (::std::size_t index = 0; index < result.size(); ++index)
	{
		result[index] = index;
	}
	return result;
}();

template<class Self, class Dependency, class Env>
[[nodiscard]] consteval info pred_completions()
{
	infos values;
	bool has_values = true;
	// 范围必须是常量表达式：用变量模板，不用局部变量。
	template for (constexpr ::std::size_t position : positions_v<Dependency>)
	{
		constexpr ::std::size_t from = dependency_indices_v<Self, Dependency>[position];
		static_assert(from < count_of_v<Self>, "sdag: 占位符引用了图里不存在的键");
		using completions = node_completions_t<Self, from, Env>;
		if constexpr (!general::is_stdexec_completion_signatures_v<completions>)
		{
			return ::std::meta::dealias(^^completions);
		}
		else
		{
			using trait = general::completion_trait<completions>;
			if (!trait::has_values)
			{
				has_values = false;
			}
			for (info value : arguments(^^typename trait::values))
			{
				values.push_back(::std::meta::substitute(^^access_t, {^^Dependency, ::std::meta::reflect_constant(position), value}));
			}
		}
	}
	return general::refl::make_completions(has_values, list(values), ^^general::type_list<>, false);
}

}

template<class Self, class Dependency, class Env>
using pred_completions_t = typename [: detail::pred_completions<Self, Dependency, Env>() :];

#else

namespace detail
{

using ::std::size_t;

template<class Dependency, size_t K>
struct accessor
{
	template<class V>
	using fn = access_t<Dependency, K, V>;
};

template<class Self, class Dependency, class Env, class Positions, class Indices>
struct pred_completions;

/// K... 是位置（0, 1, ...），D... 是对应前驱的节点下标。
template<class Self, class Dependency, class Env, size_t... K, size_t... D>
struct pred_completions<Self, Dependency, Env, ::std::index_sequence<K...>, ::std::index_sequence<D...>>
{
	static_assert(((D < count_of_v<Self>) && ...), "sdag: 占位符引用了图里不存在的键");

	template<size_t J>
	using completions_of = node_completions_t<Self, J, Env>;

	template<size_t J>
	using trait = general::completion_trait<completions_of<J>>;

	/// 按声明顺序第一个算不出签名的前驱。
	template<size_t... Rest>
	struct first_invalid
	{
		using type = void;
	};

	template<size_t First, size_t... Rest>
	struct first_invalid<First, Rest...>
	{
		using type = ::std::conditional_t<general::is_stdexec_completion_signatures_v<completions_of<First>>,
			typename first_invalid<Rest...>::type,
			completions_of<First>>;
	};

	static consteval auto compute() noexcept
	{
		if constexpr (!(general::is_stdexec_completion_signatures_v<completions_of<D>> && ...))
		{
			return ::std::type_identity<typename first_invalid<D...>::type>{};
		}
		else if constexpr (!(trait<D>::has_values && ...))
		{
			return ::std::type_identity<::stdexec::completion_signatures<>>{};
		}
		else
		{
			return ::std::type_identity<general::make_completions_t<true, general::concat_t<
				general::transform_t<accessor<Dependency, K>::template fn, typename trait<D>::values>...>, general::type_list<>, false>>{};
		}
	}

	using type = typename decltype(compute())::type;
};

}

template<class Self, class Dependency, class Env>
using pred_completions_t = typename detail::pred_completions<Self, Dependency, Env,
	::std::make_index_sequence<Dependency::keys.size()>, general::sequence_t<dependency_indices_v<Self, Dependency>>>::type;

#endif

}

}
