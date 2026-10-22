#pragma once

/// 节点：键（编译期身份）+ 依赖声明 + 载荷。
///
/// 原语：
///   - node_key<N> / node_dependencies<N>   —— 类型特征，可特化
///   - payload(n)                           —— CPO，成员优先，其次 ADL
/// concept：
///   - dependency_list<T>                   —— after<Keys...>
///   - graph_node<N>                        —— 三个原语齐备
///   - node_payload<P, Input>               —— 载荷能变成 sender：本身是 sender（只有控制依赖），
///                                             或者是接受输入 sender 的适配器（数据依赖）

#include <concepts>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "meta.h"

namespace sdag
{

/// 依赖声明：本节点在 Keys... 对应的节点全部完成后才启动，并按此顺序接收它们的值。
template<class... Keys>
struct after
{
	using keys = meta::type_list<Keys...>;
};

template<class T>
inline constexpr bool is_after = false;

template<class... Keys>
inline constexpr bool is_after<after<Keys...>> = true;

template<class T>
concept dependency_list = is_after<::std::remove_cvref_t<T>>;

// ---------------------------------------------------------------- 原语：类型特征

template<class Node>
struct node_key
{
};

template<class Node>
	requires requires { typename ::std::remove_cvref_t<Node>::key_type; }
struct node_key<Node>
{
	using type = typename ::std::remove_cvref_t<Node>::key_type;
};

template<class Node>
	requires requires { typename node_key<Node>::type; }
using node_key_t = typename node_key<Node>::type;

template<class Node>
struct node_dependencies
{
};

template<class Node>
	requires requires { typename ::std::remove_cvref_t<Node>::dependencies_type; }
struct node_dependencies<Node>
{
	using type = typename ::std::remove_cvref_t<Node>::dependencies_type;
};

template<class Node>
	requires requires { typename node_dependencies<Node>::type; }
using node_dependencies_t = typename node_dependencies<Node>::type;

// ---------------------------------------------------------------- 原语：payload CPO

namespace payload_cpo
{

enum class choose
{
	none,
	member,
	adl,
};

struct choice_result
{
	choose strategy;
	bool nothrow;
};

void payload(); // 普通查找屏障：下面的非限定 payload(...) 只走 ADL

/// 按实参类型（含值类别）选择策略。GCC 16 不允许在 noexcept / requires 里引用 auto&& 形参本身，
/// 所以这里以类型为参数、用 declval 构造与 operator() 里完全同形的表达式。
template<class Node>
[[nodiscard]] consteval choice_result choice() noexcept
{
	if constexpr (requires { ::std::declval<Node>().payload(); })
	{
		return {choose::member, noexcept(::std::declval<Node>().payload())};
	}
	else if constexpr (requires { payload(::std::declval<Node>()); })
	{
		return {choose::adl, noexcept(payload(::std::declval<Node>()))};
	}
	else
	{
		return {choose::none, true};
	}
}

struct payload_t
{
	template<class Node>
	static constexpr decltype(auto) operator()(Node&& node)
		noexcept(choice<Node&&>().nothrow)
		requires (choice<Node&&>().strategy != choose::none)
	{
		constexpr choose strategy = choice<Node&&>().strategy;
		if constexpr (strategy == choose::member)
		{
			return ::std::forward<Node>(node).payload();
		}
		else
		{
			return payload(::std::forward<Node>(node));
		}
	}
};

}

/// 取出节点载荷，保持节点的值类别：右值节点给出右值载荷。
inline constexpr payload_cpo::payload_t payload{};

// ---------------------------------------------------------------- concept

template<class Node>
concept graph_node =
	requires { typename node_key_t<Node>; }
	&& requires { typename node_dependencies_t<Node>; }
	&& dependency_list<node_dependencies_t<Node>>
	&& requires(Node&& node) { ::sdag::payload(::std::forward<Node>(node)); };

/// 载荷本身是 sender：依赖只表达先后，不接收前驱的值。
template<class Payload>
concept sender_payload = ::stdexec::sender<Payload>;

/// 载荷是 sender 适配器（例如 ::stdexec::then(f)）：喂给它输入 sender 得到节点 sender。
template<class Payload, class Input>
concept adaptor_payload =
	!sender_payload<Payload>
	&& ::std::invocable<Payload, Input>
	&& ::stdexec::sender<::std::invoke_result_t<Payload, Input>>;

template<class Payload, class Input>
concept node_payload = sender_payload<Payload> || adaptor_payload<Payload, Input>;

// ---------------------------------------------------------------- 默认节点类型

template<class Key, dependency_list Dependencies, class Payload>
struct node
{
	using key_type = Key;
	using dependencies_type = Dependencies;
	using payload_type = Payload;

	SDAG_NO_UNIQUE_ADDRESS Payload _payload;

	template<class Self>
	[[nodiscard]] constexpr auto&& payload(this Self&& self) noexcept
	{
		return ::std::forward<Self>(self)._payload;
	}
};

/// make_node<key>(sender)                   —— 根节点
/// make_node<key, after<a, b>>(then(f))     —— 依赖 a、b，f 收到 (a 的值..., b 的值...)
/// make_node<key, after<a>>(sender)         —— 只在 a 之后启动，不接收 a 的值
template<class Key, dependency_list Dependencies = after<>, class Payload>
[[nodiscard]] constexpr node<Key, Dependencies, ::std::decay_t<Payload>> make_node(Payload&& payload)
	noexcept(::std::is_nothrow_constructible_v<::std::decay_t<Payload>, Payload>)
{
	return {::std::forward<Payload>(payload)};
}

}
