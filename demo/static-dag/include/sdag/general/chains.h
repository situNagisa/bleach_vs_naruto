#pragma once

/// 偏序的最小链划分：一组元素里哪些可以依次共用同一块存储。
///
///   min_chain_cover(before, present)   before[u][v] 表示 u 必然先于 v（要求传递、无环）；present[u] 为假的元素不参与。
///                                      每个元素分到一条链（chain_cover::head[u] 是它所在链的第一个元素，position[u] 是它在链里的位置，
///                                      size[u] 是这条链的长度）。同一条链里的元素两两可比，存活区间依次排开，可以共用一块存储。
///
/// 链数最少 = 最大反链 = 最多同时存在的元素个数（Dilworth）。做法是二分图最大匹配：u 的"出点"连 v 的"入点"当且仅当
/// before[u][v]；每条匹配边把 v 接在 u 后面，链数 = 元素数 - 匹配数。

#include <array>
#include <cstddef>

namespace sdag::general
{

template<::std::size_t N>
struct chain_cover
{
	::std::array<::std::size_t, N> head{};
	::std::array<::std::size_t, N> position{};
	::std::array<::std::size_t, N> size{};
};

namespace chains_detail
{

template<::std::size_t N>
struct matching
{
	::std::array<::std::array<bool, N>, N> const& before;
	::std::array<bool, N> const& present;
	/// next[u]：链里紧跟在 u 后面的元素；prev[v]：紧挨在 v 前面的元素；都以 N 表示没有。
	::std::array<::std::size_t, N> next{};
	::std::array<::std::size_t, N> prev{};

	/// 从 u 出发找增广路：给 u 找一个后继 v，v 已经被占了就让占它的元素换一个。
	consteval bool augment(::std::size_t u, ::std::array<bool, N>& visited)
	{
		for (::std::size_t v = 0; v < N; ++v)
		{
			if (!present[v] || !before[u][v] || visited[v])
			{
				continue;
			}
			visited[v] = true;
			if (prev[v] == N || augment(prev[v], visited))
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

template<::std::size_t N>
[[nodiscard]] consteval chain_cover<N> min_chain_cover(
	::std::array<::std::array<bool, N>, N> const& before, ::std::array<bool, N> const& present)
{
	chains_detail::matching<N> match{before, present};
	match.next.fill(N);
	match.prev.fill(N);
	for (::std::size_t u = 0; u < N; ++u)
	{
		if (present[u])
		{
			::std::array<bool, N> visited{};
			match.augment(u, visited);
		}
	}

	chain_cover<N> cover{};
	for (::std::size_t head = 0; head < N; ++head)
	{
		if (!present[head] || match.prev[head] != N)
		{
			continue;
		}
		::std::size_t length = 0;
		for (::std::size_t u = head; u != N; u = match.next[u])
		{
			cover.head[u] = head;
			cover.position[u] = length++;
		}
		for (::std::size_t u = head; u != N; u = match.next[u])
		{
			cover.size[u] = length;
		}
	}
	return cover;
}

}
