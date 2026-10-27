#pragma once

/// 节点：规格（键 + 依赖，编译期的值）+ 载荷（运行期的 sender / 适配器）。
///
/// 规格是一个值，库只经 CPO 读它，所以用户可以用自己的类型：
///   node_spec_of<N, K>                    概念：sdag::id(n) 给出 K，sdag::dependencies(n) 给出一个 range，
///                                         range 的每个元素 e 满足 sdag::id(e) 给出 K、sdag::mode(e) 给出 sdag::mode
///   mode::let / mode::move / mode::wait   依赖怎么用前驱：
///       let    读：拿到 V const&，值留在整图里、活到整图结束
///       move   取走：按值（move）拿到，本节点结束时析构（之后它占的空间可以给别的值用）。一个值至多被 move 一次，
///              move 它的节点必须排在它所有其它读者之后（编译期检查，见 assembly/dag.h）
///       wait   只等先后，不接收、不碰值
///   let 与 move 的前驱按 dependencies 里的顺序把值交给载荷（wait 不占位置），可以交错。
///
/// 库给的便利类型：basic_node{id, let(a), move(b), wait(c)}（依赖个数由构造推导，不定长）。
///
/// 载荷由依赖决定、不从载荷类型去猜：有 let / move 依赖时载荷必须是 sender 适配器（dag 把前驱占位符喂给它，
/// 例如 then(f) 的节点 sender 是 then(占位符, f)），否则必须是 sender。写错了各有一条 static_assert。
///
///   make<spec>(payload)       把规格与载荷绑成一个节点；dag<K>(nodes...) 收的就是这个
///   bound_node<Spec, P>       make 的结果

#include <array>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "../topology/dependencies.h"

namespace sdag
{

/// 依赖怎么用前驱的值（定义在拓扑层，与键、sender 无关）。
using topology::mode;

// ---------------------------------------------------------------- CPO：id / dependencies / mode
//
// 成员函数优先（n.id()），其次 ADL 找到的自由函数（id(n)）。

namespace spec_cpo
{

void id();
void dependencies();
void mode();

struct id_t
{
	template<class T>
		requires requires(T const& t) { t.id(); } || requires(T const& t) { id(t); }
	[[nodiscard]] static constexpr decltype(auto) operator()(T const& t) noexcept
	{
		if constexpr (requires { t.id(); })
		{
			return t.id();
		}
		else
		{
			return id(t);
		}
	}
};

struct dependencies_t
{
	template<class T>
		requires requires(T const& t) { t.dependencies(); } || requires(T const& t) { dependencies(t); }
	[[nodiscard]] static constexpr decltype(auto) operator()(T const& t) noexcept
	{
		if constexpr (requires { t.dependencies(); })
		{
			return t.dependencies();
		}
		else
		{
			return dependencies(t);
		}
	}
};

struct mode_t
{
	template<class T>
		requires requires(T const& t) { t.mode(); } || requires(T const& t) { mode(t); }
	[[nodiscard]] static constexpr decltype(auto) operator()(T const& t) noexcept
	{
		if constexpr (requires { t.mode(); })
		{
			return t.mode();
		}
		else
		{
			return mode(t);
		}
	}
};

}

inline constexpr spec_cpo::id_t id{};
inline constexpr spec_cpo::dependencies_t dependencies{};
inline constexpr spec_cpo::mode_t mode_of{};

// ---------------------------------------------------------------- 概念

/// 键：节点的编译期身份。键的类型由用户在建图时给出（dag<K>）。
template<class K>
concept key_value = ::std::equality_comparable<K> && ::std::is_trivially_copyable_v<K>;

template<class D, class K>
concept dependency_of = requires(D const& d) {
	{ ::sdag::id(d) } -> ::std::convertible_to<K>;
	{ ::sdag::mode_of(d) } -> ::std::convertible_to<mode>;
};

template<class N, class K>
concept node_spec_of = key_value<K>
	&& requires(N const& n) {
		{ ::sdag::id(n) } -> ::std::convertible_to<K>;
		{ ::sdag::dependencies(n) } -> ::std::ranges::forward_range;
	}
	&& dependency_of<::std::ranges::range_value_t<decltype(::sdag::dependencies(::std::declval<N const&>()))>, K>;

// ---------------------------------------------------------------- 便利类型

template<key_value K>
struct basic_dependency
{
	K _id;
	::sdag::mode _mode;

	[[nodiscard]] constexpr K id() const noexcept
	{
		return _id;
	}

	[[nodiscard]] constexpr ::sdag::mode mode() const noexcept
	{
		return _mode;
	}
};

template<key_value K>
[[nodiscard]] constexpr basic_dependency<K> let(K id) noexcept
{
	return {id, mode::let};
}

template<key_value K>
[[nodiscard]] constexpr basic_dependency<K> move(K id) noexcept
{
	return {id, mode::move};
}

template<key_value K>
[[nodiscard]] constexpr basic_dependency<K> wait(K id) noexcept
{
	return {id, mode::wait};
}

/// basic_node{id, let(a), move(b), wait(c)}：依赖个数 N 由构造推导。
template<key_value K, ::std::size_t N>
struct basic_node
{
	K _id;
	::std::array<basic_dependency<K>, N> _dependencies;

	template<class... Dependencies>
		requires (sizeof...(Dependencies) == N && (::std::same_as<Dependencies, basic_dependency<K>> && ...))
	constexpr explicit basic_node(K id, Dependencies... dependencies) noexcept
		: _id(id)
		, _dependencies{dependencies...}
	{
	}

	[[nodiscard]] constexpr K id() const noexcept
	{
		return _id;
	}

	[[nodiscard]] constexpr auto const& dependencies() const noexcept
	{
		return _dependencies;
	}
};

template<key_value K, class... Dependencies>
basic_node(K, Dependencies...) -> basic_node<K, sizeof...(Dependencies)>;

// ---------------------------------------------------------------- 规格 + 载荷

/// 一个节点：编译期的规格 Spec 与运行期的载荷。
template<auto Spec, class Payload>
struct bound_node
{
	static constexpr auto spec = Spec;

	SDAG_NO_UNIQUE_ADDRESS Payload _payload;
};

/// make<spec>(payload)：把规格与载荷绑成一个节点。
template<auto Spec, class Payload>
[[nodiscard]] constexpr bound_node<Spec, ::std::decay_t<Payload>> make(Payload&& payload)
	noexcept(::std::is_nothrow_constructible_v<::std::decay_t<Payload>, Payload>)
{
	return {::std::forward<Payload>(payload)};
}

template<class T>
inline constexpr bool is_bound_node_v = false;

template<auto Spec, class Payload>
inline constexpr bool is_bound_node_v<bound_node<Spec, Payload>> = true;

/// 节点 N 是键类型为 K 的图里的合法节点：make 的结果，规格满足 node_spec_of<K>。
template<class N, class K>
concept node_of = is_bound_node_v<::std::remove_cvref_t<N>> && node_spec_of<decltype(::std::remove_cvref_t<N>::spec), K>;

/// 载荷本身是 sender：没有 let / move 依赖的节点。
template<class Payload>
concept sender_payload = ::stdexec::sender<Payload>;

/// 载荷是 sender 适配器（例如 ::stdexec::then(f)）：喂给它前驱占位符得到节点 sender。
template<class Payload, class Hole>
concept adaptor_payload =
	!sender_payload<Payload>
	&& ::std::invocable<Payload, Hole>
	&& ::stdexec::sender<::std::invoke_result_t<Payload, Hole>>;

}
