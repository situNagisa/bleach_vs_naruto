#pragma once

/// 节点：键（编译期身份，一个值）+ 依赖声明 + 载荷。键的类型由用户定，受 key_value 约束；一张图的键必须同一类型。
///
/// 依赖分两种，写在 make_node 的键后面，各至多一个，数据依赖在前：
///   data<a, b>      数据依赖：a、b 完成后启动，并按声明顺序接收它们的值。载荷必须是 sender 适配器——
///                   dag 把一个前驱占位符喂给它，make_node<c, data<a, b>>(then(f)) 的节点 sender 就是 then(占位符, f)，
///                   占位符启动时交出 (a 的值..., b 的值...)。任何"接收一个 sender、返回一个 sender"的可调用对象都行。
///   control<a>      控制依赖：只在 a 完成后启动，不接收、不碰它的值。载荷（没有数据依赖时）必须是 sender。
///   make_node<d, data<b>, control<c>>(then(f))   两种都有：等 b、c 都完成，f 只收到 b 的值。
///
/// 值的交付：data<a> 里的 a 默认以 V const& 交给本节点，值留在整图里，活到整图结束。
/// 写成 data<take{a}> 则本节点按值（move）拿到它，并在本节点结束时析构它（之后它占的空间可以给别的值用）。
/// 一个值至多被 take 一次，take 它的节点必须排在它所有其它读者之后（编译期检查，见 assembly/dag.h）。
///
/// 载荷是什么由依赖声明决定，不从载荷类型去猜：声明了 data<...> 却给了 sender、或者没有 data<...> 却给了适配器，都是编译错误。
///
/// 原语：
///   - node_key<N>::value / node_dependencies<N>::type   —— 可特化；键是值（node_key_v<N>），依赖是 dependencies<data<...>, control<...>>
///   - payload(n)                           —— CPO，成员优先，其次 ADL
/// concept：
///   - dependency_list<T>                   —— dependencies<data<...>, control<...>>
///   - graph_node<N>                        —— 三个原语齐备
///   - sender_payload<P>                    —— 载荷是 sender（没有数据依赖的节点）
///   - adaptor_payload<P, Hole>             —— 载荷是接受前驱占位符的 sender 适配器（有数据依赖的节点）

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../config.h"

namespace sdag
{

/// 键：节点的编译期身份，是一个值（非类型模板实参）。键的类型由用户自己定（枚举、整数、可比较的结构体……），
/// 一张图里所有键必须是同一个类型（编译期检查）。
template<class T>
concept key_value = ::std::equality_comparable<T>;

/// 写在 data<...> 里：取走 key 的值——本节点按值（move）拿到它，本节点结束时析构它。写作 take{key}。
template<key_value Key>
struct take
{
	Key key;
};

namespace dependency_detail
{

template<class T>
inline constexpr bool is_take_v = false;

template<class Key>
inline constexpr bool is_take_v<take<Key>> = true;

/// take{k} 换成 k，其它原样。
template<class T>
[[nodiscard]] consteval auto key_of(T value)
{
	if constexpr (is_take_v<T>)
	{
		return value.key;
	}
	else
	{
		return value;
	}
}

/// 没有任何键时的占位类型（data<> / control<> / 空图）。
struct no_key
{
	friend constexpr bool operator==(no_key, no_key) noexcept = default;
};

/// 一组键排成 std::array；空时是 std::array<no_key, 0>。
template<auto... Keys>
[[nodiscard]] consteval auto key_array()
{
	if constexpr (sizeof...(Keys) == 0)
	{
		return ::std::array<no_key, 0>{};
	}
	else
	{
		static_assert((::std::same_as<decltype(Keys), decltype(Keys...[0])> && ...), "sdag: 一张图里所有键必须是同一个类型");
		return ::std::array{Keys...};
	}
}

}

/// 数据依赖：Keys... 全部完成后启动，并按此顺序接收它们的值。Keys 里可以写 take{key}。
template<auto... Keys>
struct data
{
	/// 前驱的键（take{k} 换成 k）。
	static constexpr auto keys = dependency_detail::key_array<dependency_detail::key_of(Keys)...>();
	/// 第 i 个前驱是不是 take。
	static constexpr ::std::array<bool, sizeof...(Keys)> taken{dependency_detail::is_take_v<decltype(Keys)>...};
};

/// 控制依赖：Keys... 全部完成后启动，不接收它们的值。
template<auto... Keys>
struct control
{
	static_assert(!(dependency_detail::is_take_v<decltype(Keys)> || ...), "sdag: take{...} 只能写在 data<...> 里（控制依赖不碰值）");
	static constexpr auto keys = dependency_detail::key_array<Keys...>();
};

/// 一个节点的全部依赖：数据依赖 + 控制依赖。make_node 的依赖实参经 dependencies_of_t 补齐成这个形状。
template<class Data = data<>, class Control = control<>>
struct dependencies;

template<auto... DataKeys, auto... ControlKeys>
struct dependencies<data<DataKeys...>, control<ControlKeys...>>
{
	using data_type = data<DataKeys...>;
	using control_type = control<ControlKeys...>;
	static constexpr bool has_data = sizeof...(DataKeys) != 0;
};

template<class T>
inline constexpr bool is_dependencies = false;

template<class Data, class Control>
inline constexpr bool is_dependencies<dependencies<Data, Control>> = requires { dependencies<Data, Control>::has_data; };

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

template<auto... Keys>
struct normalize<data<Keys...>>
{
	using type = dependencies<data<Keys...>>;
};

template<auto... Keys>
struct normalize<control<Keys...>>
{
	using type = dependencies<data<>, control<Keys...>>;
};

template<auto... DataKeys, auto... ControlKeys>
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
// 默认从节点类型的静态成员 key / 嵌套类型 dependencies_type 取；自定义节点类型也可以直接特化这两个模板。
// 没有时主模板是空的，于是 node_key_v / node_dependencies_t 替换失败，graph_node 为假（不是硬错误）。

template<class Node>
struct node_key
{
};

template<class Node>
	requires requires { ::std::remove_cvref_t<Node>::key; }
struct node_key<Node>
{
	static constexpr auto value = ::std::remove_cvref_t<Node>::key;
};

template<class Node>
inline constexpr auto node_key_v = node_key<Node>::value;

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
	requires { node_key<Node>::value; }
	&& key_value<::std::remove_cvref_t<decltype(node_key<Node>::value)>>
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

template<auto Key, dependency_list Dependencies, class Payload>
	requires key_value<decltype(Key)>
struct node
{
	static constexpr auto key = Key;
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
template<auto Key, class... Deps, class Payload>
	requires key_value<decltype(Key)>
[[nodiscard]] constexpr node<Key, dependencies_of_t<Deps...>, ::std::decay_t<Payload>> make_node(Payload&& payload)
	noexcept(::std::is_nothrow_constructible_v<::std::decay_t<Payload>, Payload>)
{
	return {::std::forward<Payload>(payload)};
}

}
