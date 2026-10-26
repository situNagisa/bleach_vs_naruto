#pragma once

/// 节点：键（编译期身份）+ 依赖声明 + 载荷。
///
/// 依赖分两种，写在 make_node 的键后面，各至多一个，数据依赖在前：
///   data<a, b>      数据依赖：a、b 完成后启动，并按声明顺序接收它们的值。载荷必须是 sender 适配器——
///                   dag 把一个前驱占位符喂给它，make_node<c, data<a, b>>(then(f)) 的节点 sender 就是 then(占位符, f)，
///                   占位符启动时交出 (a 的值..., b 的值...)。任何"接收一个 sender、返回一个 sender"的可调用对象都行。
///   control<a>      控制依赖：只在 a 完成后启动，不接收、不碰它的值。载荷（没有数据依赖时）必须是 sender。
///   make_node<d, data<b>, control<c>>(then(f))   两种都有：等 b、c 都完成，f 只收到 b 的值。
///
/// 载荷是什么由依赖声明决定，不从载荷类型去猜：声明了 data<...> 却给了 sender、或者没有 data<...> 却给了适配器，都是编译错误。
///
/// 原语：
///   - node_key<N> / node_dependencies<N>   —— 类型特征，可特化；node_dependencies 给出 dependencies<data<...>, control<...>>
///   - payload(n)                           —— CPO，成员优先，其次 ADL
/// concept：
///   - dependency_list<T>                   —— dependencies<data<...>, control<...>>
///   - graph_node<N>                        —— 三个原语齐备
///   - sender_payload<P>                    —— 载荷是 sender（没有数据依赖的节点）
///   - adaptor_payload<P, Hole>             —— 载荷是接受前驱占位符的 sender 适配器（有数据依赖的节点）

#include <concepts>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../config.h"
#include "../general/type_list.h"

namespace sdag
{

/// 数据依赖：Keys... 全部完成后启动，并按此顺序接收它们的值。
template<class... Keys>
struct data
{
	using keys = general::type_list<Keys...>;
};

/// 控制依赖：Keys... 全部完成后启动，不接收它们的值。
template<class... Keys>
struct control
{
	using keys = general::type_list<Keys...>;
};

/// 一个节点的全部依赖：数据依赖 + 控制依赖。make_node 的依赖实参经 dependencies_of_t 补齐成这个形状。
template<class Data = data<>, class Control = control<>>
struct dependencies;

template<class... DataKeys, class... ControlKeys>
struct dependencies<data<DataKeys...>, control<ControlKeys...>>
{
	using data_type = data<DataKeys...>;
	using control_type = control<ControlKeys...>;
	/// 全部前驱：先数据依赖，后控制依赖。
	using keys = general::type_list<DataKeys..., ControlKeys...>;
	static constexpr bool has_data = sizeof...(DataKeys) != 0;
};

template<class T>
inline constexpr bool is_dependencies = false;

template<class Data, class Control>
inline constexpr bool is_dependencies<dependencies<Data, Control>> = requires { typename dependencies<Data, Control>::keys; };

template<class T>
concept dependency_list = is_dependencies<::std::remove_cvref_t<T>>;

namespace dependency_detail
{

/// 只接受四种写法：无依赖、data<...>、control<...>、data<...> 后跟 control<...>。
template<class... Deps>
struct normalize
{
	static_assert(sizeof...(Deps) < 0, "sdag: 依赖写作 data<...>、control<...> 或 data<...>, control<...>（数据依赖在前）");
};

template<>
struct normalize<>
{
	using type = dependencies<>;
};

template<class... Keys>
struct normalize<data<Keys...>>
{
	using type = dependencies<data<Keys...>>;
};

template<class... Keys>
struct normalize<control<Keys...>>
{
	using type = dependencies<data<>, control<Keys...>>;
};

template<class... DataKeys, class... ControlKeys>
struct normalize<data<DataKeys...>, control<ControlKeys...>>
{
	using type = dependencies<data<DataKeys...>, control<ControlKeys...>>;
};

}

/// make_node<key, Deps...> 的依赖实参 → dependencies<data<...>, control<...>>。
template<class... Deps>
using dependencies_of_t = typename dependency_detail::normalize<Deps...>::type;

// ---------------------------------------------------------------- 原语：类型特征
//
// 默认从节点类型的嵌套 key_type / dependencies_type 取；自定义节点类型也可以直接特化这两个模板。
// 没有嵌套类型时主模板是空的，于是 node_key_t / node_dependencies_t 替换失败，graph_node 为假（不是硬错误）。

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

/// 按实参类型（含值类别）选择策略：成员 payload() 优先，其次 ADL 找到的 payload(node)。
/// 以类型为参数、用 declval 构造与 operator() 里完全同形的表达式：GCC 16 不允许在 noexcept / requires 里
/// 引用 auto&& 形参本身，所以没法像 skill 骨架那样把实参转发进 choice。
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

/// 载荷本身是 sender：没有数据依赖的节点。
template<class Payload>
concept sender_payload = ::stdexec::sender<Payload>;

/// 载荷是 sender 适配器（例如 ::stdexec::then(f)）：喂给它前驱占位符得到节点 sender。
template<class Payload, class Hole>
concept adaptor_payload =
	!sender_payload<Payload>
	&& ::std::invocable<Payload, Hole>
	&& ::stdexec::sender<::std::invoke_result_t<Payload, Hole>>;

// ---------------------------------------------------------------- 默认节点类型

template<class Key, dependency_list Dependencies, class Payload>
struct node
{
	using key_type = Key;
	using dependencies_type = Dependencies;

	SDAG_NO_UNIQUE_ADDRESS Payload _payload;

	template<class Self>
	[[nodiscard]] constexpr auto&& payload(this Self&& self) noexcept
	{
		return ::std::forward<Self>(self)._payload;
	}
};

/// make_node<key>(sender)                              —— 根节点
/// make_node<key, data<a, b>>(then(f))                 —— f 收到 (a 的值..., b 的值...)
/// make_node<key, control<a>>(sender)                  —— 只在 a 之后启动，不接收 a 的值
/// make_node<key, data<a>, control<b>>(then(f))        —— 等 a、b 都完成，f 只收到 a 的值
template<class Key, class... Deps, class Payload>
[[nodiscard]] constexpr node<Key, dependencies_of_t<Deps...>, ::std::decay_t<Payload>> make_node(Payload&& payload)
	noexcept(::std::is_nothrow_constructible_v<::std::decay_t<Payload>, Payload>)
{
	return {::std::forward<Payload>(payload)};
}

}
