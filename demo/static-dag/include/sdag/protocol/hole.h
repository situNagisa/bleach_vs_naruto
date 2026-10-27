#pragma once

/// 前驱占位符：节点 sender 里「这里是前驱的值」的标记，以及数它。
///
/// 用户写 make<basic_node{b, let(a)}>(then(f))。dag 把一个空的 hole<b 的下标> 喂给 then，得到 then(hole<I>, f)
/// 作为节点 b 的 sender——节点 sender 是普通的 stdexec 表达式，then 的一切（包括 domain 对 then 的定制）都照常生效。
///
/// hole 本身不存值。连接时它从接收者环境里拿到取值源（protocol/value_source.h），启动时请取值源把前驱的值交给接收者；
/// 完成签名也问取值源。所以它（以及含有它的节点 sender、整个图）是依赖环境的 sender，无环境时报 dependent_sender_error。
///
///   hole<Node>                     第 Node 个节点的占位符
///   hole_count_v<Sender>           节点 sender 里占位符的个数。只递归穿过 stdexec 的算法层（then / let_value / when_all ……）；
///                                  其它 sender 都是不透明的叶子——包括嵌套的图：图 sender 不是 stdexec 的算法 sender，
///                                  它里面的占位符属于内层图，自然不会被数到。所以占位符不需要认识图
///
/// hole_count_v 有反射与模板两种实现，用 SDAG_REFLECTION 隔开。

#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/completion_behavior.hpp>
#include <exec/completion_signatures.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../stdexec/expressions.h"
#include "value_source.h"

namespace sdag::protocol
{

/// 占位符在 start 里同步完成。
struct hole_attrs
{
	template<class Tag>
	[[nodiscard]] static constexpr auto query(::exec::get_completion_behavior_t<Tag>) noexcept
	{
		return ::exec::completion_behavior::inline_completion;
	}
};

struct _A_HOLE_WAS_CONNECTED_WITHOUT_A_VALUE_SOURCE_IN_ITS_ENVIRONMENT_;

namespace detail
{

template<::std::size_t Node, class Receiver>
struct hole_operation
{
	Receiver _receiver;

	constexpr void start() noexcept
	{
		protocol::get_value_source(::stdexec::get_env(_receiver))->template deliver<Node>(::std::move(_receiver));
	}
};

}

template<::std::size_t Node>
struct hole;

/// 是不是占位符。两种计算模式共用这一份。
namespace hole_traits
{

template<class T>
inline constexpr bool is_hole_v = false;

template<::std::size_t Node>
inline constexpr bool is_hole_v<hole<Node>> = true;

}

template<::std::size_t Node>
struct hole
{
	using sender_concept = ::stdexec::sender_t;
	/// 这个占位符属于第几个节点：取值源按这个节点的 let / move 依赖交出前驱的值。
	static constexpr ::std::size_t node = Node;

	[[nodiscard]] static constexpr hole_attrs get_env() noexcept
	{
		return {};
	}

	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures()
	{
		if constexpr (sizeof...(Env) == 0)
		{
			return ::exec::throw_compile_time_error<::stdexec::dependent_sender_error, ::stdexec::_WITH_SENDER_<Self>>();
		}
		else if constexpr (!(has_value_source<Env> && ...))
		{
			return ::exec::throw_compile_time_error<_A_HOLE_WAS_CONNECTED_WITHOUT_A_VALUE_SOURCE_IN_ITS_ENVIRONMENT_>();
		}
		else
		{
			return source_completions_t<value_source_of_t<Env...[0]>, Node, Env...[0]>{};
		}
	}

	template<::stdexec::receiver Receiver>
	[[nodiscard]] constexpr detail::hole_operation<Node, Receiver> connect(Receiver receiver) const
		noexcept(::std::is_nothrow_move_constructible_v<Receiver>)
	{
		return {::std::move(receiver)};
	}
};

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace refl
{

using namespace ::sdag::general::refl;

template<class Sender>
inline constexpr bool is_expression_v = stdexec_adapter::sender_expression<Sender>;

/// stdexec 算法 sender 的 [tag, data, child...]；不是 stdexec 算法 sender 的类型给出空列表。
[[nodiscard]] consteval infos parts(info sender)
{
	sender = plain(sender);
	if (!::std::meta::extract<bool>(::std::meta::substitute(^^is_expression_v, {sender})))
	{
		return {};
	}
	return arguments(::std::meta::substitute(^^stdexec_adapter::parts_t, {sender}));
}

[[nodiscard]] consteval ::std::size_t hole_count(info sender)
{
	if (specializes(sender, ^^hole))
	{
		return 1;
	}
	infos const all = parts(sender);
	::std::size_t total = 0;
	for (::std::size_t index = 2; index < all.size(); ++index)
	{
		total += hole_count(all[index]);
	}
	return total;
}

}

template<class Sender>
inline constexpr ::std::size_t hole_count_v = refl::hole_count(^^Sender);

#else

namespace detail
{

template<class Sender>
[[nodiscard]] consteval ::std::size_t hole_count()
{
	using plain = ::std::remove_cvref_t<Sender>;
	if constexpr (hole_traits::is_hole_v<plain>)
	{
		return 1;
	}
	else if constexpr (!stdexec_adapter::sender_expression<plain>)
	{
		return 0;
	}
	else
	{
		return []<class Tag, class Data, class... Children>(general::type_list<Tag, Data, Children...>)
		{
			return (hole_count<Children>() + ... + ::std::size_t{0});
		}(stdexec_adapter::parts_t<plain>{});
	}
}

}

template<class Sender>
inline constexpr ::std::size_t hole_count_v = detail::hole_count<Sender>();

#endif

}

}
