#pragma once

/// 有向无环图：拓扑序、入出度、根与汇点、成环检查。
///
/// 节点用下标 0..N-1 表示，与节点"是什么"无关。shape<N> 是一个字面量：edge[from][to] 表示 to 依赖 from；
/// 根是没有前驱的节点，汇点是没有后继的节点。所有结果都是编译期常量，运行期不存在。
///
///   shape<N>, shape_error           邻接矩阵、入出度、拓扑序（Kahn，入度相同的保持下标顺序），以及建图时发现的错误
///   reach_v<Shape>                  可达关系：reach_v<Shape>[u][v] 表示 u 经过一条或多条边能到 v（u 是 v 的严格祖先）
///   sinks_v / roots_v / successors_v  把编译期的汇点 / 根 / 后继列表固定成 std::array，供展开使用

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "sequence.h"

namespace sdag::general
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
		return static_cast<::std::size_t>(::std::ranges::count(in_degree, ::std::size_t{0}));
	}

	[[nodiscard]] constexpr ::std::size_t sink_count() const noexcept
	{
		return static_cast<::std::size_t>(::std::ranges::count(out_degree, ::std::size_t{0}));
	}

	/// 下面三个只在编译期用（返回 vector）；运行期要遍历时用 roots_v / sinks_v / successors_v。
	[[nodiscard]] consteval ::std::vector<::std::size_t> roots() const
	{
		return indices_where(in_degree, ::std::size_t{0});
	}

	[[nodiscard]] consteval ::std::vector<::std::size_t> sinks() const
	{
		return indices_where(out_degree, ::std::size_t{0});
	}

	/// from 的后继（索引升序）。
	[[nodiscard]] consteval ::std::vector<::std::size_t> successors(::std::size_t from) const
	{
		return indices_where(edge[from], true);
	}

	/// values 里等于 wanted 的下标（升序）。
	template<class T>
	[[nodiscard]] static consteval ::std::vector<::std::size_t> indices_where(::std::array<T, N> const& values, T wanted)
	{
		::std::vector<::std::size_t> result;
		for (::std::size_t index = 0; index < N; ++index)
		{
			if (values[index] == wanted)
			{
				result.push_back(index);
			}
		}
		return result;
	}

	constexpr void fail(shape_error what) noexcept
	{
		if (error == shape_error::none)
		{
			error = what;
		}
	}

	constexpr void add_edge(::std::size_t from, ::std::size_t to) noexcept
	{
		if (from == N)
		{
			fail(shape_error::unknown_dependency);
			return;
		}
		if (from == to)
		{
			fail(shape_error::self_dependency);
			return;
		}
		if (edge[from][to])
		{
			fail(shape_error::duplicate_dependency);
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
				// 还有没排上的节点，却没有入度为 0 的了：剩下的节点成环。
				fail(shape_error::cycle);
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

/// 可达关系（传递闭包）：按拓扑序倒着走，每个节点能到的 = 它的后继 + 后继能到的。
template<auto Shape>
inline constexpr auto reach_v = []
{
	constexpr ::std::size_t n = Shape.count;
	::std::array<::std::array<bool, n>, n> reach{};
	for (::std::size_t step = n; step-- > 0;)
	{
		::std::size_t const from = Shape.order[step];
		for (::std::size_t to = 0; to < n; ++to)
		{
			if (Shape.edge[from][to])
			{
				reach[from][to] = true;
				for (::std::size_t further = 0; further < n; ++further)
				{
					reach[from][further] = reach[from][further] || reach[to][further];
				}
			}
		}
	}
	return reach;
}();

/// 汇点 / 根 / 后继，固定成 std::array，供 general::for_each_value 与 general::sequence_t 使用。
template<auto Shape>
inline constexpr auto sinks_v = general::to_array<Shape.sink_count()>(Shape.sinks());

template<auto Shape>
inline constexpr auto roots_v = general::to_array<Shape.root_count()>(Shape.roots());

template<auto Shape, ::std::size_t From>
inline constexpr auto successors_v = general::to_array<Shape.out_degree[From]>(Shape.successors(From));

}
