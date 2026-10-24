#pragma once

/// unique_t<List>：去重，保留每个类型第一次出现的位置。

#include <type_traits>

#include "../../config.h"
#include "type_list.h"
#include "reflection.h"
#include "index_of.h"

namespace sdag::general
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace refl
{

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

}

template<class List>
using unique_t = typename [: refl::list(refl::unique(refl::arguments(^^List))) :];

#else

namespace detail
{

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

}

template<class List>
using unique_t = typename detail::unique<type_list<>, List>::type;

#endif

}

}
