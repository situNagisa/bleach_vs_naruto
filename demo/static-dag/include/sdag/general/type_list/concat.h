#pragma once

/// concat_t<Lists...>：把若干 type_list 按顺序拼成一个。

#include <vector>

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

[[nodiscard]] consteval infos concat(::std::vector<infos> const& lists)
{
	infos result;
	for (infos const& one : lists)
	{
		result.insert(result.end(), one.begin(), one.end());
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

template<class... Lists>
using concat_t = typename [: detail::concat_lists<Lists...>() :];

#else

namespace detail
{

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

}

template<class... Lists>
using concat_t = typename detail::concat<Lists...>::type;

#endif

}

}
