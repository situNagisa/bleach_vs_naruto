#pragma once

/// 图形状的编译期分析：邻接矩阵、入度 / 出度、拓扑序、错误诊断。
///
/// 输入是键表与依赖表（每个节点的 after<...>），输出是一个 shape<N> 字面量。这是普通的 constexpr 数组算法，
/// 两种计算模式共用；只有"键换成索引"交给计算层。所有结果都是编译期常量，运行期不存在。
///
/// 节点用声明顺序的下标 0..N-1 表示。edge[from][to] 表示 to 依赖 from；
/// 根是没有前驱的节点（start 时启动），汇点是没有后继的节点（它们的值组成整图的值）。

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "meta.h"
#include "node.h"
#include "compute/primitives.h"

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

	/// 下面三个只在编译期用（返回 vector）；运行期要遍历时用 roots_v / sinks_v / successors_v。
	[[nodiscard]] consteval ::std::vector<::std::size_t> roots() const
	{
		::std::vector<::std::size_t> result;
		for (::std::size_t index = 0; index < N; ++index)
		{
			if (is_root(index))
			{
				result.push_back(index);
			}
		}
		return result;
	}

	[[nodiscard]] consteval ::std::vector<::std::size_t> sinks() const
	{
		::std::vector<::std::size_t> result;
		for (::std::size_t index = 0; index < N; ++index)
		{
			if (is_sink(index))
			{
				result.push_back(index);
			}
		}
		return result;
	}

	/// from 的后继（索引升序）。
	[[nodiscard]] consteval ::std::vector<::std::size_t> successors(::std::size_t from) const
	{
		::std::vector<::std::size_t> result;
		for (::std::size_t to = 0; to < N; ++to)
		{
			if (edge[from][to])
			{
				result.push_back(to);
			}
		}
		return result;
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

/// Keys = type_list<键...>，Dependencies = type_list<after<...>...>，一一对应。不做断言，错误写在 error 里（便于测试）。
template<class Keys, class Dependencies>
[[nodiscard]] consteval auto shape_of_lists() noexcept
{
	constexpr ::std::size_t count = Keys::size;
	shape<count> result{};
	[&]<::std::size_t... I>(::std::index_sequence<I...>)
	{
		(
			[&]
			{
				if (compute::index_of_v<meta::at_t<I, Keys>, Keys> != I)
				{
					result.fail(shape_error::duplicate_key, I);
				}
				for (::std::size_t from : compute::dependency_indices_v<Keys, meta::at_t<I, Dependencies>>)
				{
					result.add_edge(from, I);
				}
			}(),
			...);
	}(::std::make_index_sequence<count>{});
	if (result.error == shape_error::none)
	{
		result.sort();
	}
	return result;
}

/// 分析节点列表；不做断言。
template<graph_node... Nodes>
[[nodiscard]] consteval shape<sizeof...(Nodes)> shape_of() noexcept
{
	return shape_of_lists<meta::type_list<node_key_t<Nodes>...>, meta::type_list<node_dependencies_t<Nodes>...>>();
}

/// 分析并把每类错误变成一条独立的 static_assert 消息。
template<class Keys, class Dependencies>
[[nodiscard]] consteval auto checked_shape_of_lists() noexcept
{
	constexpr auto result = shape_of_lists<Keys, Dependencies>();
	static_assert(result.error != shape_error::duplicate_key, "sdag: 两个节点使用了同一个键");
	static_assert(result.error != shape_error::unknown_dependency, "sdag: 节点依赖了不存在的键");
	static_assert(result.error != shape_error::self_dependency, "sdag: 节点依赖了自己");
	static_assert(result.error != shape_error::duplicate_dependency, "sdag: 节点对同一个前驱声明了两次依赖");
	static_assert(result.error != shape_error::cycle, "sdag: 依赖图有环");
	return result;
}

/// 汇点 / 根 / 后继，固定成 std::array，供 compute::for_each_value 与 compute::sequence_t 使用。
template<auto Shape>
inline constexpr auto sinks_v = meta::to_array<Shape.sink_count()>(Shape.sinks());

template<auto Shape>
inline constexpr auto roots_v = meta::to_array<Shape.root_count()>(Shape.roots());

template<auto Shape, ::std::size_t From>
inline constexpr auto successors_v = meta::to_array<Shape.out_degree[From]>(Shape.successors(From));

}
