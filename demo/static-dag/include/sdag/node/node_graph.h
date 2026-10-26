#pragma once

/// 节点集合成图：一组节点的键与依赖 → 一张有向无环图，以及键相关的错误诊断。
///
///   key_indices<Keys>(wanted)             wanted 里的键换成它们在键表 Keys（std::array）里的下标；不存在的键给出 Keys.size()
///   shape_of_lists<Keys, Dependencies>()  Keys = 键的 std::array，Dependencies = type_list<dependencies<...>...>，一一对应 → shape<N>。
///                                         边不分数据 / 控制：两种依赖都是"先后"，同一个前驱在两边各写一次算重复依赖。
///                                         不做断言，错误写在 error 里（便于测试）
///   shape_of<Nodes...>()                  同上，直接从节点取键与依赖
///   checked_shape_of_lists<...>()         同上，并把每类错误变成一条独立的 static_assert 消息
///
/// 键是值，这里全是普通的 constexpr 循环。

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <utility>

#include "../config.h"
#include "../general/type_list.h"
#include "../general/digraph.h"
#include "node.h"

namespace sdag
{

/// 一张图的键表（各节点的键按声明顺序）。
template<class... Nodes>
inline constexpr auto keys_of_v = dependency_detail::key_array<node_key_v<Nodes>...>();

/// key 在 Keys 里的位置；不存在时等于 Keys.size()。
template<auto Keys>
[[nodiscard]] constexpr ::std::size_t key_index(typename decltype(Keys)::value_type const& key) noexcept
{
	return static_cast<::std::size_t>(::std::ranges::find(Keys, key) - Keys.begin());
}

/// Wanted（键的 std::array）里每个键在 Keys 里的下标。
template<auto Keys, auto Wanted>
inline constexpr auto key_indices_v = []
{
	::std::array<::std::size_t, Wanted.size()> result{};
	if constexpr (Wanted.size() != 0)
	{
		static_assert(::std::same_as<typename decltype(Keys)::value_type, typename decltype(Wanted)::value_type>,
			"sdag: 依赖里的键与节点的键不是同一个类型");
		for (::std::size_t index = 0; index < Wanted.size(); ++index)
		{
			result[index] = key_index<Keys>(Wanted[index]);
		}
	}
	return result;
}();

/// Keys = 键的 std::array，Dependencies = type_list<dependencies<...>...>，一一对应。不做断言，错误写在 error 里（便于测试）。
template<auto Keys, class Dependencies>
[[nodiscard]] consteval auto shape_of_lists() noexcept
{
	constexpr ::std::size_t count = Keys.size();
	general::shape<count> result{};
	[&]<::std::size_t... I>(::std::index_sequence<I...>)
	{
		(
			[&]
			{
				if (key_index<Keys>(Keys[I]) != I)
				{
					result.fail(general::shape_error::duplicate_key);
				}
				using dependencies = general::at_t<I, Dependencies>;
				for (::std::size_t from : key_indices_v<Keys, dependencies::data_type::keys>)
				{
					result.add_edge(from, I);
				}
				for (::std::size_t from : key_indices_v<Keys, dependencies::control_type::keys>)
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
	return shape_of_lists<keys_of_v<Nodes...>, general::type_list<node_dependencies_t<Nodes>...>>();
}

/// 分析并把每类错误变成一条独立的 static_assert 消息。
template<auto Keys, class Dependencies>
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
