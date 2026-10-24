#pragma once

/// transform_t<Fn, List>：对 type_list 的每个元素套一层单参数模板 Fn（别名模板也可以）。

#include "../../config.h"
#include "type_list.h"
#include "reflection.h"

namespace sdag::general
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace refl
{

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

template<template<class> class Fn, class List>
using transform_t = typename [: refl::list(refl::map(^^Fn, refl::arguments(^^List))) :];

#else

namespace detail
{

template<template<class> class Fn, class List>
struct transform;

template<template<class> class Fn, class... Ts>
struct transform<Fn, type_list<Ts...>>
{
	using type = type_list<Fn<Ts>...>;
};

}

template<template<class> class Fn, class List>
using transform_t = typename detail::transform<Fn, List>::type;

#endif

}

}
