#pragma once

/// 节点集合成图：一组节点的键与依赖 → 一张有向无环图，以及键相关的错误诊断。依赖：有向无环图、节点、类型列表。
///
///   key_indices_v<Keys, L<K...>>          L<K...>（data<...> / control<...> / type_list<...>）里的键换成它们在键表 Keys（type_list）里的下标，按声明顺序放进 std::array；
///                                         不存在的键给出 Keys::size（据此报"依赖了不存在的键"）
///   shape_of_lists<Keys, Dependencies>()  Keys = type_list<键...>，Dependencies = type_list<dependencies<...>...>，一一对应 → shape<N>。
///                                         边不分数据 / 控制：两种依赖都是"先后"，同一个前驱在两边各写一次算重复依赖。
///                                         不做断言，错误写在 error 里（便于测试）
///   shape_of<Nodes...>()                  同上，直接从节点类型取键与依赖
///   checked_shape_of_lists<...>()         同上，并把每类错误变成一条独立的 static_assert 消息
///
/// key_indices_v 有反射与模板两种实现，用 SDAG_REFLECTION 隔开。

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/sequence.h"
#include "../general/digraph.h"
#include "node.h"

namespace sdag
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

template<class Keys, class Dependency>
inline constexpr auto key_indices_v = general::to_array<general::refl::arguments(^^Dependency).size()>(
	general::refl::indices_of(general::refl::arguments(^^Dependency), general::refl::arguments(^^Keys)));

#else

namespace detail
{

template<class Keys, class Dependency>
struct key_indices;

template<class Keys, template<class...> class List, class... Dependencies>
struct key_indices<Keys, List<Dependencies...>>
{
	static constexpr ::std::array<::std::size_t, sizeof...(Dependencies)> value{general::index_of_v<Dependencies, Keys>...};
};

}

template<class Keys, class Dependency>
inline constexpr auto key_indices_v = detail::key_indices<Keys, Dependency>::value;

#endif

}

/// Keys = type_list<键...>，Dependencies = type_list<dependencies<...>...>，一一对应。不做断言，错误写在 error 里（便于测试）。
template<class Keys, class Dependencies>
[[nodiscard]] consteval auto shape_of_lists() noexcept
{
	constexpr ::std::size_t count = Keys::size;
	general::shape<count> result{};
	[&]<::std::size_t... I>(::std::index_sequence<I...>)
	{
		(
			[&]
			{
				if (general::index_of_v<general::at_t<I, Keys>, Keys> != I)
				{
					result.fail(general::shape_error::duplicate_key, I);
				}
				for (::std::size_t from : key_indices_v<Keys, typename general::at_t<I, Dependencies>::keys>)
				{
					result.add_edge(from, I);
				}
			}(),
			...);
	}(::std::make_index_sequence<count>{});
	if (result.error == general::shape_error::none)
	{
		result.sort();
	}
	return result;
}

/// 分析节点列表；不做断言。
template<graph_node... Nodes>
[[nodiscard]] consteval general::shape<sizeof...(Nodes)> shape_of() noexcept
{
	return shape_of_lists<general::type_list<node_key_t<Nodes>...>, general::type_list<node_dependencies_t<Nodes>...>>();
}

/// 分析并把每类错误变成一条独立的 static_assert 消息。
template<class Keys, class Dependencies>
[[nodiscard]] consteval auto checked_shape_of_lists() noexcept
{
	constexpr auto result = shape_of_lists<Keys, Dependencies>();
	static_assert(result.error != general::shape_error::duplicate_key, "sdag: 两个节点使用了同一个键");
	static_assert(result.error != general::shape_error::unknown_dependency, "sdag: 节点依赖了不存在的键");
	static_assert(result.error != general::shape_error::self_dependency, "sdag: 节点依赖了自己");
	static_assert(result.error != general::shape_error::duplicate_dependency, "sdag: 节点对同一个前驱声明了两次依赖");
	static_assert(result.error != general::shape_error::cycle, "sdag: 依赖图有环");
	return result;
}

}
