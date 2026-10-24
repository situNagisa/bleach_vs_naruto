#pragma once

/// 有向无环图：拓扑序、入出度、根与汇点、成环检查。依赖：无。
///
/// 节点用下标 0..N-1 表示，与节点"是什么"无关。shape<N> 是一个字面量：edge[from][to] 表示 to 依赖 from；
/// 根是没有前驱的节点，汇点是没有后继的节点。所有结果都是编译期常量，运行期不存在。
///
///   shape<N>, shape_error           邻接矩阵、入出度、拓扑序（Kahn，入度相同的保持下标顺序），以及建图时发现的错误
///   sinks_v / roots_v / successors_v  把编译期的汇点 / 根 / 后继列表固定成 std::array，供展开使用

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

/// 汇点 / 根 / 后继，固定成 std::array，供 general::for_each_value 与 general::sequence_t 使用。
template<auto Shape>
inline constexpr auto sinks_v = general::to_array<Shape.sink_count()>(Shape.sinks());

template<auto Shape>
inline constexpr auto roots_v = general::to_array<Shape.root_count()>(Shape.roots());

template<auto Shape, ::std::size_t From>
inline constexpr auto successors_v = general::to_array<Shape.out_degree[From]>(Shape.successors(From));

}
