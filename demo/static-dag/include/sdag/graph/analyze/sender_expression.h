#pragma once

/// 节点 sender 里的输入叶子：数一数（input_count_v），或者换掉（substituted_t）。
///
///   input_count_v<Sender>           输入叶子的个数。递归穿过 stdexec 的算法层（then / let_value / when_all ……）；
///                                   嵌套的图是另一张图的节点，它里面的输入叶子属于内层图，不算；
///                                   自定义 sender 拆不开，当作不透明的叶子（数成 0）。
///   substituted_t<Sender, Input>    输入叶子换成 Input 之后的类型。沿 stdexec 的算法层重建：标签、data 不变，
///                                   含输入叶子的子树递归替换，其余子树原样保留。重建出的是裸的 concurrency::bridge::expression_t：
///                                   stdexec 的算法 sender 都是它的空派生类，行为一致。
///
/// 拆 stdexec 算法 sender 用 stdexec_bridge.h（只认 stdexec 自己的算法 sender）。

#include <cstddef>
#include <type_traits>

#include "../../config.h"
#include "../../meta_program/type_list.h"
#include "../../meta_program/utility.h"
#include "../../meta_program/indexed_tuple.h"
#include "../../fwd.h"
#include "../../concurrency/stdexec_bridge.h"
#include "../../meta_program/reflection.h"

namespace sdag::analyze
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace refl
{

using namespace ::sdag::meta::refl;

template<class Sender>
inline constexpr bool is_expression_v = concurrency::bridge::sender_expression<Sender>;

/// stdexec 算法 sender 的 [tag, data, child...]；不是 stdexec 算法 sender 的类型给出空列表。
[[nodiscard]] consteval infos parts(info sender)
{
	sender = plain(sender);
	if (!::std::meta::extract<bool>(::std::meta::substitute(^^is_expression_v, {sender})))
	{
		return {};
	}
	return arguments(::std::meta::substitute(^^concurrency::bridge::parts_t, {sender}));
}

[[nodiscard]] consteval ::std::size_t input_count(info sender)
{
	if (specializes(sender, ^^input_sender))
	{
		return 1;
	}
	if (specializes(sender, ^^graph_sender))
	{
		return 0;
	}
	infos const all = parts(sender);
	::std::size_t total = 0;
	for (::std::size_t index = 2; index < all.size(); ++index)
	{
		total += input_count(all[index]);
	}
	return total;
}

/// input_count 只穿过算法层去数叶子，所以这里遇到的含叶子的 sender 一定拆得开。
[[nodiscard]] consteval info substituted(info sender, info input)
{
	sender = plain(sender);
	if (specializes(sender, ^^input_sender))
	{
		return ::std::meta::decay(input);
	}
	if (input_count(sender) == 0)
	{
		return sender;
	}
	infos const all = parts(sender);
	infos rebuilt{all[0], all[1]};
	for (::std::size_t index = 2; index < all.size(); ++index)
	{
		rebuilt.push_back(substituted(all[index], input));
	}
	return ::std::meta::substitute(^^concurrency::bridge::expression_t, rebuilt);
}

}

template<class Sender>
inline constexpr ::std::size_t input_count_v = refl::input_count(^^Sender);

template<class Sender, class Input>
using substituted_t = typename [: refl::substituted(^^Sender, ^^Input) :];

#else

namespace detail
{

template<class T>
inline constexpr bool is_input_v = false;

template<class Dependency>
inline constexpr bool is_input_v<input_sender<Dependency>> = true;

template<class T>
inline constexpr bool is_graph_v = false;

template<class Data, class... Children>
inline constexpr bool is_graph_v<graph_sender<Data, Children...>> = true;

template<class Sender>
[[nodiscard]] consteval ::std::size_t input_count()
{
	using plain = ::std::remove_cvref_t<Sender>;
	if constexpr (is_input_v<plain>)
	{
		return 1;
	}
	else if constexpr (is_graph_v<plain> || !concurrency::bridge::sender_expression<plain>)
	{
		return 0;
	}
	else
	{
		return []<class Tag, class Data, class... Children>(meta::type_list<Tag, Data, Children...>)
		{
			return (input_count<Children>() + ... + ::std::size_t{0});
		}(concurrency::bridge::parts_t<plain>{});
	}
}

template<class Sender, class Input>
struct substituted;

template<class Parts, class Input>
struct rebuilt;

template<class Tag, class Data, class... Children, class Input>
struct rebuilt<meta::type_list<Tag, Data, Children...>, Input>
{
	using type = concurrency::bridge::expression_t<Tag, Data, typename substituted<Children, Input>::type...>;
};

template<class Sender, class Input>
struct substituted
{
	using plain = ::std::remove_cvref_t<Sender>;

	static consteval auto compute() noexcept
	{
		if constexpr (is_input_v<plain>)
		{
			return ::std::type_identity<::std::decay_t<Input>>{};
		}
		else if constexpr (input_count<plain>() == 0)
		{
			return ::std::type_identity<plain>{};
		}
		else
		{
			return ::std::type_identity<typename rebuilt<concurrency::bridge::parts_t<plain>, Input>::type>{};
		}
	}

	using type = typename decltype(compute())::type;
};

}

template<class Sender>
inline constexpr ::std::size_t input_count_v = detail::input_count<Sender>();

template<class Sender, class Input>
using substituted_t = typename detail::substituted<Sender, Input>::type;

#endif

}

}
