#pragma once

/// 节点集合成图：一组节点规格 → 一张表（每个节点的依赖：前驱下标 + 用法）与一张有向无环图，以及键相关的错误诊断。
///
///   dependency_entry               表里的一项：前驱的下标（键不存在时等于节点数）+ mode
///   graph_table<N, E>              N 个节点、共 E 个依赖；节点 I 的依赖是 entries[first[I], first[I + 1])；shape 是图的形状；
///                                  dependencies(I)：节点 I 的依赖（span）；has_values(I)：节点 I 有没有 let / move 依赖
///   make_table<K, Specs...>()      由规格建表（键在这里一次换成下标，之后全用下标）。不做断言，错误写在 shape.error 里
///   checked_table<K, Specs...>()   同上，并把每类错误变成一条独立的 static_assert 消息
///   shape_of<K, Specs...>()        只要形状（便于测试）
///
/// 边不分 let / move / wait：都是"先后"，同一个前驱写两次（哪怕用法不同）算重复依赖。

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <span>

#include "../config.h"
#include "../general/digraph.h"
#include "node.h"

namespace sdag
{

struct dependency_entry
{
	::std::size_t from;
	mode how;
};

template<::std::size_t N, ::std::size_t E>
struct graph_table
{
	::std::array<::std::size_t, N + 1> first{};
	::std::array<dependency_entry, E> entries{};
	general::shape<N, E> shape{};

	/// 节点 node 的依赖（前驱下标 + mode，按声明顺序）。
	[[nodiscard]] constexpr ::std::span<dependency_entry const> dependencies(::std::size_t node) const noexcept
	{
		return ::std::span{entries}.subspan(first[node], first[node + 1] - first[node]);
	}

	/// 节点 node 有没有 let / move 依赖（决定载荷是适配器还是 sender）。
	[[nodiscard]] constexpr bool has_values(::std::size_t node) const noexcept
	{
		return ::std::ranges::any_of(dependencies(node), [](dependency_entry const& entry) { return entry.how != mode::wait; });
	}
};

/// 由规格建表：键在这里一次换成下标（不存在的键给出节点数），之后全用下标。不做断言，错误写在 shape.error 里。
template<key_value K, auto... Specs>
	requires (node_spec_of<decltype(Specs), K> && ...)
[[nodiscard]] constexpr auto make_table() noexcept
{
	constexpr ::std::size_t count = sizeof...(Specs);
	constexpr ::std::size_t total = (::std::size_t{0} + ... + static_cast<::std::size_t>(::std::ranges::distance(::sdag::dependencies(Specs))));
	graph_table<count, total> table{};
	::std::array<K, count> const keys{static_cast<K>(::sdag::id(Specs))...};
	auto index_of = [&](K const& key)
	{
		return static_cast<::std::size_t>(::std::ranges::find(keys, key) - keys.begin());
	};

	general::shape_error error = general::shape_error::none;
	::std::array<::std::size_t, total> preds{};
	::std::size_t node = 0;
	::std::size_t cursor = 0;
	auto add = [&](auto const& spec)
	{
		if (index_of(keys[node]) != node)
		{
			error = general::shape_error::duplicate_key;
		}
		table.first[node] = cursor;
		for (auto const& dependency : ::sdag::dependencies(spec))
		{
			::std::size_t const from = index_of(static_cast<K>(::sdag::id(dependency)));
			table.entries[cursor] = {from, static_cast<mode>(::sdag::mode_of(dependency))};
			preds[cursor++] = from;
		}
		++node;
	};
	(add(Specs), ...);
	table.first[count] = cursor;
	table.shape = general::shape<count, total>::build(table.first, preds, error);
	return table;
}

template<key_value K, auto... Specs>
[[nodiscard]] constexpr auto shape_of() noexcept
{
	return make_table<K, Specs...>().shape;
}

/// 建表并把每类错误变成一条独立的 static_assert 消息。
template<key_value K, auto... Specs>
[[nodiscard]] consteval auto checked_table() noexcept
{
	constexpr auto table = make_table<K, Specs...>();
	constexpr auto error = table.shape.error;
	static_assert(error != general::shape_error::duplicate_key, "sdag: 两个节点使用了同一个键");
	static_assert(error != general::shape_error::unknown_dependency, "sdag: 节点依赖了不存在的键");
	static_assert(error != general::shape_error::self_dependency, "sdag: 节点依赖了自己");
	static_assert(error != general::shape_error::duplicate_dependency, "sdag: 节点对同一个前驱声明了两次依赖");
	static_assert(error != general::shape_error::cycle, "sdag: 依赖图有环");
	return table;
}

}
