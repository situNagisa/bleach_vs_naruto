#pragma once

/// 前驱的签名：占位符的签名 = 前驱节点的值签名（共享的给 const&）；实现取值源的「签名接口」。
/// 依赖：图的描述、节点的签名、值的读者、编译期序列、完成签名、类型列表、取值源协议。
///
///   pred_completions_t<Self, data<...>, Env>
///       前驱的值按 data<...> 的声明顺序拼接：只有一个读者的值给 V（move 过去），多个读者共享的值给 V const&；
///       某个前驱没有值完成（例如 just_stopped）时是 completion_signatures<>（这个节点永远不会被启动）；
///       某个前驱的签名算不出来时，是那个不合法的结果本身（原样透传给 stdexec 诊断）。
///   pred_signatures<Self>
///       取值源的签名接口的实现：completions_for<Dependency, Env> = pred_completions_t<Self, Dependency, Env>
///
/// 只看前驱节点的签名，不碰整图分析：整图分析要算每个节点的签名，节点的签名又要问它的占位符——
/// 如果占位符反过来要整图分析，就成环了。

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
#include "node_signature.h"

namespace sdag::graph
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace detail
{

using namespace ::sdag::general::refl;

template<class Self, class Dependency, class Env>
[[nodiscard]] consteval info pred_completions()
{
	infos values;
	bool has_values = true;
	template for (constexpr ::std::size_t from : dependency_indices_v<Self, Dependency>)
	{
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
				values.push_back(access_type(value, shared_v<Self, from>));
			}
		}
	}
	if (!has_values)
	{
		return ^^::stdexec::completion_signatures<>;
	}
	return general::refl::value_completions(list(values));
}

}

template<class Self, class Dependency, class Env>
using pred_completions_t = typename [: detail::pred_completions<Self, Dependency, Env>() :];

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
struct pred_completions;

template<class Self, class Env, size_t... D>
struct pred_completions<Self, Env, ::std::index_sequence<D...>>
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
			return ::std::type_identity<general::value_completions_t<general::concat_t<
				general::transform_t<accessor<shared_v<Self, D>>::template fn, typename trait<D>::values>...>>>{};
		}
	}

	using type = typename decltype(compute())::type;
};

}

template<class Self, class Dependency, class Env>
using pred_completions_t = typename detail::pred_completions<Self, Env,
	general::sequence_t<dependency_indices_v<Self, Dependency>>>::type;

#endif

}

/// 取值源的签名接口（见 protocol/value_source.h）。整图状态经 protocol::value_source_traits 的特化使用它（graph/slots.h）。
template<class Self>
struct pred_signatures
{
	template<class Dependency, class Env>
	using completions_for = pred_completions_t<Self, Dependency, Env>;
};

}
