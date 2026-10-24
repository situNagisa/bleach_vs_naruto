#pragma once

/// index_of_v<T, List>：T 在 type_list List 里第一次出现的位置；不存在时等于 List::size。
/// 反射版另有 refl::index_of / refl::indices_of 供其它模块复用。

#include <array>
#include <cstddef>
#include <vector>
#include <type_traits>

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
