#pragma once

/// 有向无环图：节点用下标 0..N-1 表示，与节点"是什么"无关。边以前驱表给出（压缩邻接：节点 v 的前驱是
/// preds[first[v], first[v + 1])）。算法都是普通的 constexpr 函数，输入输出是 span / mdspan，编译期与运行期都能用，
/// 存储由调用方给。
///
///   shape_error                                       建图时发现的错误
///   node_error(v, predecessors)                       节点 v 的前驱里有没有未知 / 自依赖 / 重复
///   invert(first, preds, succ_first, succs)           前驱表 -> 后继表（每个节点的后继按下标升序）
///   topological_order(first, succ_first, succs, order)   Kahn：就绪节点里取下标最小的，独立节点保持声明顺序；成环返回 false
///   reachability(order, succ_first, succs, reach)     传递闭包：reach[u, v] 表示 u 经一条或多条边能到 v
///   shape<N, E>                                       固定容量的一张图（N 个节点、E 条边）：上面这些算一遍存起来

#include <algorithm>
#include <array>
#include <cstddef>
#include <mdspan>
#include <span>
#include <vector>

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

using bool_matrix = ::std::mdspan<bool, ::std::dextents<::std::size_t, 2>>;
using const_bool_matrix = ::std::mdspan<bool const, ::std::dextents<::std::size_t, 2>>;

/// 节点 v 的前驱（n 个节点的图里）：下标等于 n 表示不存在的键。
[[nodiscard]] constexpr shape_error node_error(::std::size_t n, ::std::size_t v, ::std::span<::std::size_t const> predecessors) noexcept
{
	for (::std::size_t index = 0; index < predecessors.size(); ++index)
	{
		::std::size_t const from = predecessors[index];
		if (from >= n)
		{
			return shape_error::unknown_dependency;
		}
		if (from == v)
		{
			return shape_error::self_dependency;
		}
		if (::std::ranges::find(predecessors.first(index), from) != predecessors.begin() + static_cast<::std::ptrdiff_t>(index))
		{
			return shape_error::duplicate_dependency;
		}
	}
	return shape_error::none;
}

/// 前驱表 -> 后继表。succ_first 长度 n + 1，succs 长度与 preds 相同。
constexpr void invert(::std::span<::std::size_t const> first, ::std::span<::std::size_t const> preds,
	::std::span<::std::size_t> succ_first, ::std::span<::std::size_t> succs) noexcept
{
	::std::size_t const n = first.size() - 1;
	::std::ranges::fill(succ_first, ::std::size_t{0});
	for (::std::size_t from : preds)
	{
		++succ_first[from + 1];
	}
	for (::std::size_t v = 0; v < n; ++v)
	{
		succ_first[v + 1] += succ_first[v];
	}
	::std::vector<::std::size_t> cursor(succ_first.begin(), succ_first.end() - 1);
	for (::std::size_t to = 0; to < n; ++to)
	{
		for (::std::size_t e = first[to]; e < first[to + 1]; ++e)
		{
			succs[cursor[preds[e]]++] = to;
		}
	}
}

/// Kahn 算法；每轮取最小下标的就绪节点，所以独立节点按声明顺序排。成环时返回 false。
[[nodiscard]] constexpr bool topological_order(::std::span<::std::size_t const> first,
	::std::span<::std::size_t const> succ_first, ::std::span<::std::size_t const> succs, ::std::span<::std::size_t> order) noexcept
{
	::std::size_t const n = first.size() - 1;
	::std::vector<::std::size_t> remaining(n);
	::std::vector<bool> placed(n);
	for (::std::size_t v = 0; v < n; ++v)
	{
		remaining[v] = first[v + 1] - first[v];
	}
	for (::std::size_t step = 0; step < n; ++step)
	{
		::std::size_t pick = n;
		for (::std::size_t v = 0; v < n; ++v)
		{
			if (!placed[v] && remaining[v] == 0)
			{
				pick = v;
				break;
			}
		}
		if (pick == n)
		{
			// 还有没排上的节点，却没有入度为 0 的了：剩下的节点成环。
			return false;
		}
		placed[pick] = true;
		order[step] = pick;
		for (::std::size_t e = succ_first[pick]; e < succ_first[pick + 1]; ++e)
		{
			--remaining[succs[e]];
		}
	}
	return true;
}

/// 传递闭包：按拓扑序倒着走，每个节点能到的 = 它的后继 + 后继能到的。reach 是 n × n。
constexpr void reachability(::std::span<::std::size_t const> order, ::std::span<::std::size_t const> succ_first,
	::std::span<::std::size_t const> succs, bool_matrix reach) noexcept
{
	::std::size_t const n = order.size();
	for (::std::size_t step = n; step-- > 0;)
	{
		::std::size_t const from = order[step];
		for (::std::size_t e = succ_first[from]; e < succ_first[from + 1]; ++e)
		{
			::std::size_t const to = succs[e];
			reach[from, to] = true;
			for (::std::size_t further = 0; further < n; ++further)
			{
				reach[from, further] = reach[from, further] || reach[to, further];
			}
		}
	}
}

/// 固定容量的一张图：N 个节点、E 条边。由前驱表建出，入出度、根与汇点、后继、拓扑序、可达关系都在这里算好。
template<::std::size_t N, ::std::size_t E>
struct shape
{
	static constexpr ::std::size_t count = N;

	::std::array<::std::size_t, N + 1> first{};
	::std::array<::std::size_t, E> preds{};
	::std::array<::std::size_t, N + 1> succ_first{};
	::std::array<::std::size_t, E> succs{};
	::std::array<::std::size_t, N> in_degree{};
	::std::array<::std::size_t, N> out_degree{};
	/// 拓扑序；入度相同的节点保持声明顺序。
	::std::array<::std::size_t, N> order{};
	::std::array<::std::size_t, N> root_list{};
	::std::array<::std::size_t, N> sink_list{};
	::std::size_t roots_size = 0;
	::std::size_t sinks_size = 0;
	::std::array<bool, N * N> reach_storage{};
	shape_error error = shape_error::none;

	/// 由前驱表建图。error 已经不是 none 时（例如调用方发现了重复键）只记下前驱表，不再往下算。
	[[nodiscard]] static constexpr shape build(::std::array<::std::size_t, N + 1> const& first,
		::std::array<::std::size_t, E> const& preds, shape_error error = shape_error::none) noexcept
	{
		shape result{};
		result.first = first;
		result.preds = preds;
		result.error = error;
		for (::std::size_t v = 0; v < N && result.error == shape_error::none; ++v)
		{
			result.error = node_error(N, v, result.predecessors(v));
		}
		if (result.error != shape_error::none)
		{
			return result;
		}
		invert(result.first, result.preds, result.succ_first, result.succs);
		for (::std::size_t v = 0; v < N; ++v)
		{
			result.in_degree[v] = first[v + 1] - first[v];
			result.out_degree[v] = result.succ_first[v + 1] - result.succ_first[v];
			if (result.in_degree[v] == 0)
			{
				result.root_list[result.roots_size++] = v;
			}
			if (result.out_degree[v] == 0)
			{
				result.sink_list[result.sinks_size++] = v;
			}
		}
		if (!topological_order(result.first, result.succ_first, result.succs, result.order))
		{
			result.error = shape_error::cycle;
			return result;
		}
		reachability(result.order, result.succ_first, result.succs, bool_matrix(result.reach_storage.data(), N, N));
		return result;
	}

	[[nodiscard]] constexpr ::std::span<::std::size_t const> predecessors(::std::size_t v) const noexcept
	{
		return ::std::span{preds}.subspan(first[v], first[v + 1] - first[v]);
	}

	/// 后继（下标升序）。
	[[nodiscard]] constexpr ::std::span<::std::size_t const> successors(::std::size_t v) const noexcept
	{
		return ::std::span{succs}.subspan(succ_first[v], succ_first[v + 1] - succ_first[v]);
	}

	/// 根 / 汇点（下标升序）。
	[[nodiscard]] constexpr ::std::span<::std::size_t const> roots() const noexcept
	{
		return ::std::span{root_list}.first(roots_size);
	}

	[[nodiscard]] constexpr ::std::span<::std::size_t const> sinks() const noexcept
	{
		return ::std::span{sink_list}.first(sinks_size);
	}

	[[nodiscard]] constexpr ::std::size_t root_count() const noexcept
	{
		return roots_size;
	}

	[[nodiscard]] constexpr ::std::size_t sink_count() const noexcept
	{
		return sinks_size;
	}

	[[nodiscard]] constexpr bool is_root(::std::size_t v) const noexcept
	{
		return in_degree[v] == 0;
	}

	[[nodiscard]] constexpr bool is_sink(::std::size_t v) const noexcept
	{
		return out_degree[v] == 0;
	}

	/// reach()[u, v]：u 经一条或多条边能到 v（u 是 v 的严格祖先）。
	[[nodiscard]] constexpr const_bool_matrix reach() const noexcept
	{
		return const_bool_matrix(reach_storage.data(), N, N);
	}
};

}
