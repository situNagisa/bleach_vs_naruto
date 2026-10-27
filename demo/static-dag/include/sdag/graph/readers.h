#pragma once

/// 值的读者：谁读谁的值（let）、谁取走谁的值（move）。
///
///   find_readers(table, read, taker)    普通 constexpr 函数（编译期 / 运行期都能用）：read[J] 节点 J 的值有没有 let / move 读者；
///                                       taker[J] 被哪个节点 move（没有时等于节点数）。返回 move 用法的错误（move_error）
///   readers<N>::build(table)            固定容量的结果；整张图的那一份缓存在 graph_data::readers（graph/description.h）
///
/// 规则只有一条：move 就按值交出，并在取走它的节点结束时析构；let 给 const&，值活到整图结束。

#include <array>
#include <cstddef>
#include <span>

#include "../config.h"
#include "../node/node_graph.h"

namespace sdag::graph
{

enum class move_error
{
	none,
	/// 同一个值被 move 了两次。
	taken_twice,
	/// move 它的节点之后还有别的读者（或与它并行的读者）：move 会和那个读者的读冲突。
	reader_after_taker,
};

/// 读者与取走者。table 要求形状合法（shape.error == none）。
template<class Table>
[[nodiscard]] constexpr move_error find_readers(Table const& table, ::std::span<bool> read, ::std::span<::std::size_t> taker) noexcept
{
	::std::size_t const count = read.size();
	auto const reach = table.shape.reach();
	move_error error = move_error::none;
	::std::ranges::fill(read, false);
	::std::ranges::fill(taker, count);
	for (::std::size_t node = 0; node < count; ++node)
	{
		for (dependency_entry const& entry : table.dependencies(node))
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
		for (dependency_entry const& entry : table.dependencies(node))
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

template<::std::size_t N>
struct readers
{
	::std::array<bool, N> read{};
	::std::array<::std::size_t, N> taker{};
	move_error error = move_error::none;

	template<class Table>
	[[nodiscard]] static constexpr readers build(Table const& table) noexcept
	{
		readers result{};
		result.error = find_readers(table, result.read, result.taker);
		return result;
	}
};

}
