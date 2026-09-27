#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace bvn
{
/// 值键的类型；主模板仅移除引用后读取 key_type，保留 cv。
/// 默认实现要求 key_type 存在；未提供时须特化本 trait。
template<class Graph>
struct dependency_graph_key
{
	using type = typename ::std::remove_reference_t<Graph>::key_type;
};

/// 按 Graph 原样查找 trait 特化，不归一化其 cv/ref。
template<class Graph>
	requires requires { typename dependency_graph_key<Graph>::type; }
using dependency_graph_key_t = typename dependency_graph_key<Graph>::type;

namespace contains_cpo
{
// 零参数重载阻断普通查找；两参数调用交由 ADL。
constexpr void contains() noexcept {}

enum class choose
{
	member,
	adl,
	none,
};

struct choice_result
{
	choose strategy;
	bool nothrow;
};

consteval choice_result choice(auto&& graph, auto&& key) noexcept
{
	if constexpr (requires { ::std::forward<decltype(graph)>(graph).contains(::std::forward<decltype(key)>(key)); })
	{
		return {choose::member, noexcept(::std::forward<decltype(graph)>(graph).contains(::std::forward<decltype(key)>(key)))};
	}
	else if constexpr (requires { contains(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)); })
	{
		return {choose::adl, noexcept(contains(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)))};
	}
	else
	{
		return {choose::none, true};
	}
}

struct contains_t
{
	[[nodiscard]] constexpr
#if __cpp_static_call_operator
	static
#endif
	decltype(auto) operator()(auto&& graph, auto&& key)
#if !__cpp_static_call_operator
		const
#endif
		noexcept(::bvn::contains_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).nothrow)
		requires (::bvn::contains_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).strategy != choose::none)
	{
		constexpr auto strategy = ::bvn::contains_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).strategy;
		if constexpr (strategy == choose::member)
		{
			return ::std::forward<decltype(graph)>(graph).contains(::std::forward<decltype(key)>(key));
		}
		else
		{
			return contains(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key));
		}
	}
};
}

/// 查询键是否已经构造完成；成员优先，其次 ADL，无默认实现。
inline constexpr contains_cpo::contains_t contains{};

namespace emplace_cpo
{
constexpr void emplace() noexcept {}

enum class choose
{
	member,
	adl,
	none,
};

struct choice_result
{
	choose strategy;
	bool nothrow;
};

consteval choice_result choice(auto&& graph, auto&& key) noexcept
{
	if constexpr (requires { ::std::forward<decltype(graph)>(graph).emplace(::std::forward<decltype(key)>(key)); })
	{
		return {choose::member, noexcept(::std::forward<decltype(graph)>(graph).emplace(::std::forward<decltype(key)>(key)))};
	}
	else if constexpr (requires { emplace(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)); })
	{
		return {choose::adl, noexcept(emplace(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)))};
	}
	else
	{
		return {choose::none, true};
	}
}

struct emplace_t
{
	constexpr
#if __cpp_static_call_operator
	static
#endif
	decltype(auto) operator()(auto&& graph, auto&& key)
#if !__cpp_static_call_operator
		const
#endif
		noexcept(::bvn::emplace_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).nothrow)
		requires (::bvn::emplace_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).strategy != choose::none)
	{
		constexpr auto strategy = ::bvn::emplace_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).strategy;
		if constexpr (strategy == choose::member)
		{
			return ::std::forward<decltype(graph)>(graph).emplace(::std::forward<decltype(key)>(key));
		}
		else
		{
			return emplace(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key));
		}
	}
};
}

/// 构造键对应的对象；成员优先，其次 ADL，无默认实现。
/// @pre 键尚未构造完成。递归构造的环检测、失败状态恢复由具体图负责。
/// @post 成功后 contains(graph, key) 为真，at(graph, key) 可用。
inline constexpr emplace_cpo::emplace_t emplace{};

namespace at_cpo
{
constexpr void at() noexcept {}

enum class choose
{
	member,
	adl,
	none,
};

struct choice_result
{
	choose strategy;
	bool nothrow;
};

consteval choice_result choice(auto&& graph, auto&& key) noexcept
{
	if constexpr (requires { ::std::forward<decltype(graph)>(graph).at(::std::forward<decltype(key)>(key)); })
	{
		return {choose::member, noexcept(::std::forward<decltype(graph)>(graph).at(::std::forward<decltype(key)>(key)))};
	}
	else if constexpr (requires { at(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)); })
	{
		return {choose::adl, noexcept(at(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)))};
	}
	else
	{
		return {choose::none, true};
	}
}

struct at_t
{
	[[nodiscard]] constexpr
#if __cpp_static_call_operator
	static
#endif
	decltype(auto) operator()(auto&& graph, auto&& key)
#if !__cpp_static_call_operator
		const
#endif
		noexcept(::bvn::at_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).nothrow)
		requires (::bvn::at_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).strategy != choose::none)
	{
		constexpr auto strategy = ::bvn::at_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).strategy;
		if constexpr (strategy == choose::member)
		{
			return ::std::forward<decltype(graph)>(graph).at(::std::forward<decltype(key)>(key));
		}
		else
		{
			return at(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key));
		}
	}
};
}

/// 访问已构造的对象，原样保留返回类型；成员优先，其次 ADL，无默认实现。
/// @pre contains(graph, key) 为真。返回对象的有效期由具体图约定。
inline constexpr at_cpo::at_t at{};

/// 按 Graph 的 cv/ref 调用原语（例如 Graph& 表示左值图），以 const& 借用键。
/// contains 的结果可隐式转为 bool；at 只要求可调用，不限制返回类型。
/// 原语不得消耗图或改变键的身份，contains/at 不改变构造状态。
template<class Graph>
concept dependency_graph = requires { typename ::bvn::dependency_graph_key_t<Graph>; }
	&& requires(Graph&& graph, ::bvn::dependency_graph_key_t<Graph> const& key)
	{
		{ ::bvn::contains(::std::forward<Graph>(graph), key) } -> ::std::convertible_to<bool>;
		::bvn::at(::std::forward<Graph>(graph), key);
	};

/// 在 dependency_graph 的契约上增加 emplace，不约束其返回值或要求不抛异常。
template<class Graph>
concept resolvable_dependency_graph = ::bvn::dependency_graph<Graph>
	&& requires(Graph&& graph, ::bvn::dependency_graph_key_t<Graph> const& key)
	{
		::bvn::emplace(::std::forward<Graph>(graph), key);
	};

namespace resolve_cpo
{
constexpr void resolve() noexcept {}

enum class choose
{
	member,
	adl,
	default_implementation,
};

struct choice_result
{
	choose strategy;
	bool nothrow;
};

consteval choice_result choice(auto&& graph, auto&& key) noexcept
	requires ::bvn::resolvable_dependency_graph<decltype(graph)>
{
	using key_reference = ::bvn::dependency_graph_key_t<decltype(graph)> const&;
	constexpr auto key_nothrow = ::std::is_nothrow_convertible_v<decltype(key), key_reference>;
	if constexpr (requires { ::std::forward<decltype(graph)>(graph).resolve(::std::declval<key_reference>()); })
	{
		return {choose::member, key_nothrow && noexcept(::std::forward<decltype(graph)>(graph).resolve(::std::declval<key_reference>()))};
	}
	else if constexpr (requires { resolve(::std::forward<decltype(graph)>(graph), ::std::declval<key_reference>()); })
	{
		return {choose::adl, key_nothrow && noexcept(resolve(::std::forward<decltype(graph)>(graph), ::std::declval<key_reference>()))};
	}
	else
	{
		return {choose::default_implementation, key_nothrow
			&& noexcept(static_cast<bool>(::bvn::contains(::std::forward<decltype(graph)>(graph), ::std::declval<key_reference>())))
			&& noexcept(::bvn::emplace(::std::forward<decltype(graph)>(graph), ::std::declval<key_reference>()))
			&& noexcept(::bvn::at(::std::forward<decltype(graph)>(graph), ::std::declval<key_reference>()))};
	}
}

struct resolve_t
{
	[[nodiscard]] constexpr
#if __cpp_static_call_operator
	static
#endif
	decltype(auto) operator()(auto&& graph, auto&& key)
#if !__cpp_static_call_operator
		const
#endif
		noexcept(::bvn::resolve_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).nothrow)
		requires ::bvn::resolvable_dependency_graph<decltype(graph)>
			&& ::std::convertible_to<decltype(key), ::bvn::dependency_graph_key_t<decltype(graph)> const&>
	{
		::bvn::dependency_graph_key_t<decltype(graph)> const& lookup_key = ::std::forward<decltype(key)>(key);
		constexpr auto strategy = ::bvn::resolve_cpo::choice(::std::forward<decltype(graph)>(graph), ::std::forward<decltype(key)>(key)).strategy;
		if constexpr (strategy == choose::member)
		{
			return ::std::forward<decltype(graph)>(graph).resolve(lookup_key);
		}
		else if constexpr (strategy == choose::adl)
		{
			return resolve(::std::forward<decltype(graph)>(graph), lookup_key);
		}
		else
		{
			if (!static_cast<bool>(::bvn::contains(::std::forward<decltype(graph)>(graph), lookup_key)))
			{
				static_cast<void>(::bvn::emplace(::std::forward<decltype(graph)>(graph), lookup_key));
			}
			return ::bvn::at(::std::forward<decltype(graph)>(graph), lookup_key);
		}
	}
};
}

/// 取得或按需构造键对应的对象：成员 resolve 优先，其次 ADL，最后组合三个原语。
/// 默认实现仅在 contains 为假时 emplace，随后返回 at 的结果（保留引用）。
/// 键在整个调用期间按 const& 借用；返回引用不延长临时图的生命周期。
/// 不额外同步；构造状态、递归依赖、销毁顺序与异常恢复均由具体图维护。
inline constexpr resolve_cpo::resolve_t resolve{};
}
