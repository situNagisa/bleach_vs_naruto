#pragma once

/// dependency_indices_v<Keys, after<K...>>：把 after<...> 里的键换成它们在键表 Keys（type_list）里的下标，
/// 按声明顺序放进 std::array。不存在的键给出 Keys::size（shape.h 据此报"依赖了不存在的键"）。

#include <array>
#include <cstddef>
#include <vector>

#include "../config.h"
#include "../meta_program/type_list.h"
#include "../meta_program/utility.h"
#include "../meta_program/indexed_tuple.h"
#include "../meta_program/reflection.h"
#include "../meta_program/index_of.h"

namespace sdag::meta
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace refl
{

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

template<class Keys, class Dependency>
inline constexpr auto dependency_indices_v = meta::to_array<refl::arguments(^^Dependency).size()>(
	refl::indices_of(refl::arguments(^^Dependency), refl::arguments(^^Keys)));

#else

namespace detail
{

template<class Keys, class Dependency>
struct dependency_indices;

template<class Keys, template<class...> class After, class... Dependencies>
struct dependency_indices<Keys, After<Dependencies...>>
{
	static constexpr ::std::array<::std::size_t, sizeof...(Dependencies)> value{index_of_v<Dependencies, Keys>...};
};

}

template<class Keys, class Dependency>
inline constexpr auto dependency_indices_v = detail::dependency_indices<Keys, Dependency>::value;

#endif

}

}
