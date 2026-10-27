#pragma once

/// 依赖的用法与值的生命周期：谁读谁的值（let）、谁取走谁的值（move），以及由此得到的值槽先后关系。
/// 只认节点下标，与键、sender、类型都无关；都是普通 constexpr 函数，编译期与运行期都能用，存储由调用方给。
///
///   mode::let / mode::move / mode::wait          依赖怎么用前驱（语义见 node/node.h）
///   dependency_entry{from, how}                  一条依赖：前驱下标 + 用法。节点 v 的依赖是 entries[first[v], first[v + 1])
///   move_error                                   move 用法的错误
///   find_readers(first, entries, reach, read, taker)
///                                                read[J]：J 的值有没有 let / move 读者；taker[J]：J 被哪个节点 move（没有时等于节点数）
///   released_before(taker, reach, before)        before[u, v]：u 的值必然在 v 产生值之前析构（u 被 move，取走它的节点是 v 的严格祖先）。
///                                                交给 min_chain_cover（chains.h）就得到值槽的复用方案
///   readers<N>::build(first, entries, reach)     固定容量的 read / taker / error
///
/// 规则只有一条：move 就按值交出，并在取走它的节点结束时析构；let 给 const&，值活到整图结束。

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "digraph.h"

namespace sdag::topology
{

/// 依赖怎么用前驱的值。
enum class mode : unsigned char
{
	let,
	move,
	wait,
};

struct dependency_entry
{
	::std::size_t from;
	mode how;
};

enum class move_error
{
	none,
	/// 同一个值被 move 了两次。
	taken_twice,
	/// move 它的节点之后还有别的读者（或与它并行的读者）：move 会和那个读者的读冲突。
	reader_after_taker,
};

/// 读者与取走者。要求形状合法（reach 是这张图的可达关系）。
[[nodiscard]] constexpr move_error find_readers(::std::span<::std::size_t const> first, ::std::span<dependency_entry const> entries,
	const_bool_matrix reach, ::std::span<bool> read, ::std::span<::std::size_t> taker) noexcept
{
	::std::size_t const count = read.size();
	auto dependencies = [&](::std::size_t node) { return entries.subspan(first[node], first[node + 1] - first[node]); };
	move_error error = move_error::none;
	::std::ranges::fill(read, false);
	::std::ranges::fill(taker, count);
	for (::std::size_t node = 0; node < count; ++node)
	{
		for (dependency_entry const& entry : dependencies(node))
		{
			if (entry.how == mode::wait)
			{
				continue;
			}
			read[entry.from] = true;
			if (entry.how == mode::move)
			{
				if (taker[entry.from] != count && error == move_error::none)
				{
					error = move_error::taken_twice;
				}
				taker[entry.from] = node;
			}
		}
	}
	// move 它的节点必须是它所有其它读者的严格后代：其它读者读完（const&）之后才轮到 move。
	for (::std::size_t node = 0; node < count; ++node)
	{
		for (dependency_entry const& entry : dependencies(node))
		{
			::std::size_t const owner = taker[entry.from];
			if (entry.how == mode::let && owner != count && !reach[node, owner] && error == move_error::none)
			{
				error = move_error::reader_after_taker;
			}
		}
	}
	return error;
}

/// 值的先后：before[u, v] 当且仅当 u 被 move，且取走它的节点是 v 的严格祖先——u 在取走者结束时析构，v 的值在那之后才产生。
constexpr void released_before(::std::span<::std::size_t const> taker, const_bool_matrix reach, bool_matrix before) noexcept
{
	::std::size_t const count = taker.size();
	for (::std::size_t u = 0; u < count; ++u)
	{
		for (::std::size_t v = 0; v < count; ++v)
		{
			before[u, v] = taker[u] != count && reach[taker[u], v];
		}
	}
}

template<::std::size_t N>
struct readers
{
	::std::array<bool, N> read{};
	::std::array<::std::size_t, N> taker{};
	move_error error = move_error::none;

	[[nodiscard]] static constexpr readers build(::std::span<::std::size_t const> first, ::std::span<dependency_entry const> entries,
		const_bool_matrix reach) noexcept
	{
		readers result{};
		result.error = find_readers(first, entries, reach, result.read, result.taker);
		return result;
	}
};

}
