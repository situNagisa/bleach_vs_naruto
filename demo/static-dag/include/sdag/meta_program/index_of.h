#pragma once

/// index_of_v<T, List>：T 在 type_list List 里第一次出现的位置；不存在时等于 List::size。

#include <array>
#include <cstddef>
#include <type_traits>

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

namespace refl
{

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

}

template<class T, class List>
inline constexpr ::std::size_t index_of_v = refl::index_of(^^T, refl::arguments(^^List));

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

}

template<class T, class List>
inline constexpr ::std::size_t index_of_v = detail::index_of<T, List>::value;

#endif

}

}
