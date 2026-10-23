#pragma once

/// rebind_t<To, List>：把 type_list 的元素喂给另一个模板，rebind_t<std::tuple, type_list<int, char>> 是 std::tuple<int, char>。

#include "../config.h"
#include "../meta_program/type_list.h"
#include "../meta_program/utility.h"
#include "../meta_program/indexed_tuple.h"
#include "../meta_program/reflection.h"

namespace sdag::meta
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

template<template<class...> class To, class List>
using rebind_t = typename [: ::std::meta::substitute(^^To, refl::arguments(^^List)) :];

#else

namespace detail
{

template<template<class...> class To, class List>
struct rebind;

template<template<class...> class To, class... Ts>
struct rebind<To, type_list<Ts...>>
{
	using type = To<Ts...>;
};

}

template<template<class...> class To, class List>
using rebind_t = typename detail::rebind<To, List>::type;

#endif

}

}
