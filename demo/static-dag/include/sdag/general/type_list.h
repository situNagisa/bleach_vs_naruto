#pragma once

/// 类型列表：一组类型怎么查、去重、拼接、映射。
///
///   type_list<Ts...>, at_t<I, List>
///   index_of_v<T, List>       T 第一次出现的位置，不存在时等于 List::size
///   concat_t<Lists...>        按顺序拼接
///   unique_t<List>            去重，保留首次出现的顺序
///   transform_t<Fn, List>     每个元素套一层单参数模板
///   rebind_t<To, List>        元素喂给另一个模板：rebind_t<std::tuple, type_list<int, char>> 是 std::tuple<int, char>
///   general::refl::*          反射实现共用的基础（arguments、list、plain、index_of、indices_of……）
///
/// 写法约定（凡是有两种实现的文件都一样）：两种实现用 #if SDAG_REFLECTION 隔开，对外名字完全相同。
/// 反射实现里类型用 std::meta::info 表示，类型列表就是 std::vector<info>（infos），运算都是普通的 consteval 函数，
/// 对外的名字再用 [: :] 拼回类型；跨文件复用的 consteval 函数放在 general::refl，只在本文件里用的细节放在 detail。

#include <array>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "../config.h"

#if SDAG_REFLECTION
#include <meta>
#endif

namespace sdag::general
{

template<class... Ts>
struct type_list
{
	static constexpr ::std::size_t size = sizeof...(Ts);
};

template<::std::size_t I, class List>
struct at;

template<::std::size_t I, class... Ts>
struct at<I, type_list<Ts...>>
{
	using type = Ts...[I];
};

template<::std::size_t I, class List>
using at_t = typename at<I, List>::type;

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace refl
{

using info = ::std::meta::info;
using infos = ::std::vector<info>;

/// 模板特化的实参（先去掉别名）。
[[nodiscard]] consteval infos arguments(info type)
{
	return ::std::meta::template_arguments_of(::std::meta::dealias(type));
}

/// type_list<types...>
[[nodiscard]] consteval info list(infos const& types)
{
	return ::std::meta::substitute(^^type_list, types);
}

[[nodiscard]] consteval bool same(info left, info right)
{
	return ::std::meta::dealias(left) == ::std::meta::dealias(right);
}

/// 去别名、去 cv / 引用。对 sender 类型做任何检查之前都先做这一步。
[[nodiscard]] consteval info plain(info type)
{
	return ::std::meta::remove_cvref(::std::meta::dealias(type));
}

/// type 是不是模板 primary 的特化（先 plain）。
[[nodiscard]] consteval bool specializes(info type, info primary)
{
	type = plain(type);
	return ::std::meta::has_template_arguments(type) && ::std::meta::template_of(type) == primary;
}

/// libstdc++ 的 std::array<T, 0> 没有 consteval 的 operator[]，装不了 consteval-only 的 info；N 为 0 时留一个占位元素。
template<::std::size_t N>
using info_array = ::std::array<info, N == 0 ? 1 : N>;

[[nodiscard]] consteval ::std::size_t index_of(info type, infos const& types)
{
	for (::std::size_t index = 0; index < types.size(); ++index)
	{
		if (same(type, types[index]))
		{
			return index;
		}
	}
	return types.size();
}

/// wanted 里每个类型在 keys 里的位置。
[[nodiscard]] consteval ::std::vector<::std::size_t> indices_of(infos const& wanted, infos const& keys)
{
	::std::vector<::std::size_t> result;
	for (info key : wanted)
	{
		result.push_back(index_of(key, keys));
	}
	return result;
}

[[nodiscard]] consteval infos concat(::std::vector<infos> const& lists)
{
	infos result;
	for (infos const& one : lists)
	{
		result.insert(result.end(), one.begin(), one.end());
	}
	return result;
}

[[nodiscard]] consteval infos unique(infos const& types)
{
	infos result;
	for (info type : types)
	{
		if (index_of(type, result) == result.size())
		{
			result.push_back(type);
		}
	}
	return result;
}

/// function 是单参数模板的反射。
[[nodiscard]] consteval infos map(info function, infos const& types)
{
	infos result;
	for (info type : types)
	{
		result.push_back(::std::meta::dealias(::std::meta::substitute(function, {type})));
	}
	return result;
}

}

namespace detail
{

/// 别名模板里不能直接写 {arguments(^^Lists)...} 这样的临时 vector（consteval-only 的值不能出现在常量求值之外），
/// 所以包一层 consteval 函数。
template<class... Lists>
[[nodiscard]] consteval refl::info concat_lists()
{
	return refl::list(refl::concat({refl::arguments(^^Lists)...}));
}

}

template<class T, class List>
inline constexpr ::std::size_t index_of_v = refl::index_of(^^T, refl::arguments(^^List));

template<class... Lists>
using concat_t = typename [: detail::concat_lists<Lists...>() :];

template<class List>
using unique_t = typename [: refl::list(refl::unique(refl::arguments(^^List))) :];

template<template<class> class Fn, class List>
using transform_t = typename [: refl::list(refl::map(^^Fn, refl::arguments(^^List))) :];

template<template<class...> class To, class List>
using rebind_t = typename [: ::std::meta::substitute(^^To, refl::arguments(^^List)) :];

#else

namespace detail
{

template<class T, class List>
struct index_of;

template<class T, class... Ts>
struct index_of<T, type_list<Ts...>>
{
	static constexpr ::std::size_t value = []
	{
		constexpr ::std::array<bool, sizeof...(Ts)> same{::std::is_same_v<T, Ts>...};
		for (::std::size_t index = 0; index < same.size(); ++index)
		{
			if (same[index])
			{
				return index;
			}
		}
		return sizeof...(Ts);
	}();
};

template<class... Lists>
struct concat
{
	using type = type_list<>;
};

template<class... Ts>
struct concat<type_list<Ts...>>
{
	using type = type_list<Ts...>;
};

template<class... Ts, class... Us, class... Rest>
struct concat<type_list<Ts...>, type_list<Us...>, Rest...>
{
	using type = typename concat<type_list<Ts..., Us...>, Rest...>::type;
};

template<class Accumulated, class Pending>
struct unique;

template<class... Done>
struct unique<type_list<Done...>, type_list<>>
{
	using type = type_list<Done...>;
};

template<class... Done, class Next, class... Rest>
struct unique<type_list<Done...>, type_list<Next, Rest...>>
{
	using type = typename unique<
		::std::conditional_t<(::std::is_same_v<Next, Done> || ...), type_list<Done...>, type_list<Done..., Next>>,
		type_list<Rest...>>::type;
};

template<template<class> class Fn, class List>
struct transform;

template<template<class> class Fn, class... Ts>
struct transform<Fn, type_list<Ts...>>
{
	using type = type_list<Fn<Ts>...>;
};

template<template<class...> class To, class List>
struct rebind;

template<template<class...> class To, class... Ts>
struct rebind<To, type_list<Ts...>>
{
	using type = To<Ts...>;
};

}

template<class T, class List>
inline constexpr ::std::size_t index_of_v = detail::index_of<T, List>::value;

template<class... Lists>
using concat_t = typename detail::concat<Lists...>::type;

template<class List>
using unique_t = typename detail::unique<type_list<>, List>::type;

template<template<class> class Fn, class List>
using transform_t = typename detail::transform<Fn, List>::type;

template<template<class...> class To, class List>
using rebind_t = typename detail::rebind<To, List>::type;

#endif

}

}
