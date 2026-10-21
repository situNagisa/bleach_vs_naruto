#pragma once

/// 图形状的编译期分析：邻接矩阵、入度 / 出度、拓扑序、错误诊断。
/// 两种策略共用，所有结果都是 constexpr 值，运行期不存在。

#include <array>
#include <cstddef>

#include "meta.h"
#include "node.h"

namespace sdag
{

enum class shape_error
{
	none,
	duplicate_key,        // 两个节点用了同一个键
	unknown_dependency,   // 依赖了不存在的键
	self_dependency,      // 依赖自己
	duplicate_dependency, // 同一个前驱声明了两次
	cycle,                // 有环
};

template<::std::size_t N>
struct shape
{
	static constexpr ::std::size_t count = N;

	/// edge[from][to]：to 依赖 from。
	::std::array<::std::array<bool, N>, N> edge{};
	::std::array<::std::size_t, N> in_degree{};
	::std::array<::std::size_t, N> out_degree{};
	/// 拓扑序；入度相同的节点保持声明顺序。
	::std::array<::std::size_t, N> order{};
	shape_error error = shape_error::none;
	/// 出错的节点索引（声明顺序）。
	::std::size_t offending = 0;

	[[nodiscard]] constexpr bool is_root(::std::size_t index) const noexcept
	{
		return in_degree[index] == 0;
	}

	[[nodiscard]] constexpr bool is_sink(::std::size_t index) const noexcept
	{
		return out_degree[index] == 0;
	}

	[[nodiscard]] constexpr ::std::size_t root_count() const noexcept
	{
		::std::size_t total = 0;
		for (::std::size_t index = 0; index < N; ++index)
		{
			total += is_root(index) ? 1 : 0;
		}
		return total;
	}

	[[nodiscard]] constexpr ::std::size_t sink_count() const noexcept
	{
		::std::size_t total = 0;
		for (::std::size_t index = 0; index < N; ++index)
		{
			total += is_sink(index) ? 1 : 0;
		}
		return total;
	}

	constexpr void fail(shape_error what, ::std::size_t where) noexcept
	{
		if (error == shape_error::none)
		{
			error = what;
			offending = where;
		}
	}

	constexpr void add_edge(::std::size_t from, ::std::size_t to) noexcept
	{
		if (from == N)
		{
			fail(shape_error::unknown_dependency, to);
			return;
		}
		if (from == to)
		{
			fail(shape_error::self_dependency, to);
			return;
		}
		if (edge[from][to])
		{
			fail(shape_error::duplicate_dependency, to);
			return;
		}
		edge[from][to] = true;
		++in_degree[to];
		++out_degree[from];
	}

	/// Kahn 算法；每轮取最小索引的就绪节点，所以独立节点按声明顺序排。
	constexpr void sort() noexcept
	{
		::std::array<::std::size_t, N> remaining = in_degree;
		::std::array<bool, N> placed{};
		::std::size_t placed_count = 0;
		while (placed_count < N)
		{
			::std::size_t pick = N;
			for (::std::size_t index = 0; index < N; ++index)
			{
				if (!placed[index] && remaining[index] == 0)
				{
					pick = index;
					break;
				}
			}
			if (pick == N)
			{
				for (::std::size_t index = 0; index < N; ++index)
				{
					if (!placed[index])
					{
						fail(shape_error::cycle, index);
						break;
					}
				}
				return;
			}
			placed[pick] = true;
			order[placed_count++] = pick;
			for (::std::size_t to = 0; to < N; ++to)
			{
				if (edge[pick][to])
				{
					--remaining[to];
				}
			}
		}
	}
};

namespace shape_detail
{

template<class Node, class Keys, ::std::size_t N>
constexpr void add_node(shape<N>& result, ::std::size_t index) noexcept
{
	using dependencies = typename node_dependencies_t<Node>::keys;
	[&]<class... Keys2>(meta::type_list<Keys2...>)
	{
		(result.add_edge(meta::index_of_v<Keys2, Keys>, index), ...);
	}(dependencies{});
}

}

/// 分析节点列表；不做断言，错误写在 error 里（便于测试）。
template<graph_node... Nodes>
[[nodiscard]] consteval shape<sizeof...(Nodes)> shape_of() noexcept
{
	using keys = meta::type_list<node_key_t<Nodes>...>;
	shape<sizeof...(Nodes)> result{};
	::std::size_t index = 0;
	(
		[&]
		{
			if (meta::index_of_v<node_key_t<Nodes>, keys> != index)
			{
				result.fail(shape_error::duplicate_key, index);
			}
			shape_detail::add_node<Nodes, keys>(result, index);
			++index;
		}(),
		...);
	if (result.error == shape_error::none)
	{
		result.sort();
	}
	return result;
}

/// 分析并把每类错误变成一条独立的 static_assert 消息。
template<graph_node... Nodes>
[[nodiscard]] consteval shape<sizeof...(Nodes)> checked_shape() noexcept
{
	constexpr auto result = shape_of<Nodes...>();
	static_assert(result.error != shape_error::duplicate_key, "sdag: 两个节点使用了同一个键");
	static_assert(result.error != shape_error::unknown_dependency, "sdag: 节点依赖了不存在的键");
	static_assert(result.error != shape_error::self_dependency, "sdag: 节点依赖了自己");
	static_assert(result.error != shape_error::duplicate_dependency, "sdag: 节点对同一个前驱声明了两次依赖");
	static_assert(result.error != shape_error::cycle, "sdag: 依赖图有环");
	return result;
}

template<class Keys, class Dependencies>
struct keys_to_indices;

template<class Keys, class... Dependencies>
struct keys_to_indices<Keys, meta::type_list<Dependencies...>>
{
	using type = ::std::index_sequence<meta::index_of_v<Dependencies, Keys>...>;
};

/// 节点 Index 的前驱索引（std::index_sequence），按 after<...> 的声明顺序。
template<::std::size_t Index, graph_node... Nodes>
using dependency_indices_t = typename keys_to_indices<
	meta::type_list<node_key_t<Nodes>...>,
	typename node_dependencies_t<meta::at_t<Index, meta::type_list<Nodes...>>>::keys>::type;

}
