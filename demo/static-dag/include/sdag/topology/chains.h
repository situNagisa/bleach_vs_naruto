#pragma once

/// 偏序的最小链划分：一组元素里哪些可以依次共用同一块存储。
///
///   min_chain_cover(before, present, head, position, size)
///       before[u, v] 表示 u 必然先于 v（要求传递、无环）；present[u] 为假的元素不参与。
///       每个参与的元素分到一条链：head[u] 是它所在链的第一个元素，position[u] 是它在链里的位置，size[u] 是这条链的长度。
///       同一条链里的元素两两可比，存活区间依次排开，可以共用一块存储。
///   chain_cover<N>::build(before, present)   固定容量的结果
///
/// 链数最少 = 最大反链 = 最多同时存在的元素个数（Dilworth）。做法是二分图最大匹配：u 的"出点"连 v 的"入点"当且仅当
/// before[u, v]；每条匹配边把 v 接在 u 后面，链数 = 元素数 - 匹配数。普通 constexpr 函数，编译期与运行期都能用。

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "digraph.h"

namespace sdag::topology
{

namespace chains_detail
{

struct matching
{
	const_bool_matrix before;
	::std::span<bool const> present;
	/// next[u]：链里紧跟在 u 后面的元素；prev[v]：紧挨在 v 前面的元素；都以 n 表示没有。
	::std::vector<::std::size_t> next;
	::std::vector<::std::size_t> prev;

	/// 从 u 出发找增广路：给 u 找一个后继 v，v 已经被占了就让占它的元素换一个。
	constexpr bool augment(::std::size_t u, ::std::vector<bool>& visited)
	{
		::std::size_t const n = present.size();
		for (::std::size_t v = 0; v < n; ++v)
		{
			if (!present[v] || !before[u, v] || visited[v])
			{
				continue;
			}
			visited[v] = true;
			if (prev[v] == n || augment(prev[v], visited))
			{
				next[u] = v;
				prev[v] = u;
				return true;
			}
		}
		return false;
	}
};

}

constexpr void min_chain_cover(const_bool_matrix before, ::std::span<bool const> present,
	::std::span<::std::size_t> head, ::std::span<::std::size_t> position, ::std::span<::std::size_t> size)
{
	::std::size_t const n = present.size();
	chains_detail::matching match{before, present, ::std::vector<::std::size_t>(n, n), ::std::vector<::std::size_t>(n, n)};
	for (::std::size_t u = 0; u < n; ++u)
	{
		if (present[u])
		{
			::std::vector<bool> visited(n);
			match.augment(u, visited);
		}
	}
	for (::std::size_t first = 0; first < n; ++first)
	{
		if (!present[first] || match.prev[first] != n)
		{
			continue;
		}
		::std::size_t length = 0;
		for (::std::size_t u = first; u != n; u = match.next[u])
		{
			head[u] = first;
			position[u] = length++;
		}
		for (::std::size_t u = first; u != n; u = match.next[u])
		{
			size[u] = length;
		}
	}
}

template<::std::size_t N>
struct chain_cover
{
	::std::array<::std::size_t, N> head{};
	::std::array<::std::size_t, N> position{};
	::std::array<::std::size_t, N> size{};

	[[nodiscard]] static constexpr chain_cover build(const_bool_matrix before, ::std::array<bool, N> const& present)
	{
		chain_cover cover{};
		min_chain_cover(before, present, cover.head, cover.position, cover.size);
		return cover;
	}
};

}
