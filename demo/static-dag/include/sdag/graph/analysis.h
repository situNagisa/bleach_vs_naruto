#pragma once

/// 节点完成分析：每个节点可能怎么完成——有值 / 有错误 / 会停止，各是什么类型。
///
/// node_analysis<Self, Env>
///   valid                   所有节点在 Env 下都能给出完成签名
///   first_invalid_t         不合法时第一个不合法的结果（原样透传给 stdexec 诊断）；合法时为 void
///   values_t<J>             节点 J 衰变后的值类型（type_list）
///   raw_values_t<J>         节点 J 原始的值类型（保留 cv / 引用，唯一汇点直接转发时用）
///   errors_t<J>             节点 J 衰变后的错误类型（type_list）
///   nothrow_values[J]       节点 J 的值存进 op-state（衰变拷贝 / 移动）会不会抛
///   nothrow_errors[J]       节点 J 的错误存进 op-state 会不会抛
///   any_stopped             有没有节点会停止
///   any_errors              有没有节点会出错
///   all_have_values         每个节点都有值完成（否则整图永远不会以值完成——每个节点都是某个汇点的祖先）
///
/// "哪些值要存、存的时候会不会抛"不在这里——那取决于谁读这些值，是 graph/storage_plan.h 的事。
///
/// 为什么按拓扑序逐个节点算：节点 N 是 then(hole, f) 这样的表达式，then 算签名时要问占位符，占位符又要问前驱的签名——
/// 一条长链的签名计算会层层嵌套，几十个节点就撞上编译器的模板深度上限。先算前驱，后面的计算全部命中编译器已有的
/// 实例化结果，嵌套深度变成常数。反射实现用 template for 按拓扑序走；模板实现用 walk 基类链（基类先于派生类实例化），
/// 合法性与第一个不合法的结果也沿这条链一并累积，两种实现结构一一对应。
///
/// 节点不合法时不能去拆它的签名（会硬错误）：反射实现里各字段保持默认值，模板实现在 valid / invalid 两个版本之间选。

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/signatures.h"
#include "description.h"

namespace sdag::graph
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace detail
{

using namespace ::sdag::general::refl;

template<::std::size_t N>
struct node_analysis_result
{
	bool valid = true;
	info first_invalid = ^^void;

	::std::array<bool, N> nothrow_values{};
	::std::array<bool, N> nothrow_errors{};
	info_array<N> values{};
	info_array<N> raw_values{};
	info_array<N> errors{};

	bool any_stopped = false;
	bool any_errors = false;
	bool all_have_values = true;
};

template<class Self, class Env>
[[nodiscard]] consteval auto analyze_nodes()
{
	node_analysis_result<count_of_v<Self>> result;
	// 范围必须是常量表达式：用静态成员，不用局部变量。
	template for (constexpr ::std::size_t index : shape_of_v<Self>.order)
	{
		using completions = node_completions_t<Self, index, Env>;
		if constexpr (!general::is_stdexec_completion_signatures_v<completions>)
		{
			if (result.valid)
			{
				result.valid = false;
				result.first_invalid = ::std::meta::dealias(^^completions);
			}
		}
		else
		{
			using trait = general::completion_trait<completions>;
			result.raw_values[index] = ::std::meta::dealias(^^typename trait::raw_values);
			result.values[index] = ::std::meta::dealias(^^typename trait::values);
			result.errors[index] = ::std::meta::dealias(^^typename trait::errors);
			result.nothrow_values[index] = trait::nothrow_values;
			result.nothrow_errors[index] = trait::nothrow_errors;
			result.any_stopped = result.any_stopped || trait::sends_stopped;
			result.any_errors = result.any_errors || trait::errors::size != 0;
			result.all_have_values = result.all_have_values && trait::has_values;
		}
	}
	return result;
}

}

template<class Self, class Env>
struct node_analysis
{
	static constexpr auto result = detail::analyze_nodes<Self, Env>();

	static constexpr bool valid = result.valid;
	using first_invalid_t = typename [: result.first_invalid :];

	template<::std::size_t J>
	using values_t = typename [: valid ? result.values[J] : ^^general::type_list<> :];

	template<::std::size_t J>
	using raw_values_t = typename [: valid ? result.raw_values[J] : ^^general::type_list<> :];

	template<::std::size_t J>
	using errors_t = typename [: valid ? result.errors[J] : ^^general::type_list<> :];

	static constexpr auto nothrow_values = result.nothrow_values;
	static constexpr auto nothrow_errors = result.nothrow_errors;
	static constexpr bool any_stopped = result.any_stopped;
	static constexpr bool any_errors = result.any_errors;
	static constexpr bool all_have_values = result.all_have_values;
};

#else

namespace detail
{

using ::std::size_t;

/// 按拓扑序逐个节点走（见文件开头）。walk<Step> 的基类是 walk<Step - 1>，基类先实例化；
/// 每一级累积 valid 与第一个不合法的结果，与反射实现的 template for 一一对应。
template<class Self, size_t Step, class Env>
struct walk : walk<Self, Step - 1, Env>
{
	using base = walk<Self, Step - 1, Env>;
	using completions = node_completions_t<Self, shape_of_v<Self>.order[Step - 1], Env>;
	static constexpr bool ok = general::is_stdexec_completion_signatures_v<completions>;
	static constexpr bool valid = base::valid && ok;
	using first_invalid_t = ::std::conditional_t<!base::valid || ok, typename base::first_invalid_t, completions>;
};

template<class Self, class Env>
struct walk<Self, 0, Env>
{
	static constexpr bool valid = true;
	using first_invalid_t = void;
};

template<class Self, class Env>
using walk_t = walk<Self, count_of_v<Self>, Env>;

template<class Self, class Env, class Indices>
struct valid_node_analysis;

template<class Self, class Env, size_t... I>
struct valid_node_analysis<Self, Env, ::std::index_sequence<I...>>
{
	template<size_t J>
	using trait = general::completion_trait<node_completions_t<Self, J, Env>>;

	static constexpr bool valid = true;
	using first_invalid_t = void;

	template<size_t J>
	using values_t = typename trait<J>::values;

	template<size_t J>
	using raw_values_t = typename trait<J>::raw_values;

	template<size_t J>
	using errors_t = typename trait<J>::errors;

	static constexpr ::std::array<bool, sizeof...(I)> nothrow_values{trait<I>::nothrow_values...};
	static constexpr ::std::array<bool, sizeof...(I)> nothrow_errors{trait<I>::nothrow_errors...};
	static constexpr bool any_stopped = (trait<I>::sends_stopped || ...);
	static constexpr bool any_errors = ((trait<I>::errors::size != 0) || ...);
	static constexpr bool all_have_values = (trait<I>::has_values && ...);
};

template<class Self, class Env>
struct invalid_node_analysis
{
	static constexpr bool valid = false;
	using first_invalid_t = typename walk_t<Self, Env>::first_invalid_t;

	template<size_t J>
	using values_t = general::type_list<>;

	template<size_t J>
	using raw_values_t = general::type_list<>;

	template<size_t J>
	using errors_t = general::type_list<>;

	static constexpr ::std::array<bool, count_of_v<Self>> nothrow_values{};
	static constexpr ::std::array<bool, count_of_v<Self>> nothrow_errors{};
	static constexpr bool any_stopped = false;
	static constexpr bool any_errors = false;
	static constexpr bool all_have_values = false;
};

}

template<class Self, class Env>
struct node_analysis : ::std::conditional_t<detail::walk_t<Self, Env>::valid,
	detail::valid_node_analysis<Self, Env, ::std::make_index_sequence<count_of_v<Self>>>,
	detail::invalid_node_analysis<Self, Env>>
{
};

#endif

}

}
