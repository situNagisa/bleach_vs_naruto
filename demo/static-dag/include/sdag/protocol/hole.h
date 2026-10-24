#pragma once

/// 前驱占位符：节点 sender 里「这里是前驱的值」的标记，以及找它、数它、换掉它。依赖：依赖型报错、sender 表达式、类型列表、取值源协议。
///
/// 用户写 make_node<b, after<a>>(then(f))。dag 把一个空的 hole<after<a>> 喂给 then，得到 then(hole<after<a>>, f)
/// 作为节点 b 的 sender——节点 sender 是普通的 stdexec 表达式，then 的一切（包括 domain 对 then 的定制）都照常生效。
///
/// hole 本身不存值。连接时它从接收者环境里拿到取值源（protocol/value_source.h），启动时请取值源把前驱的值交给接收者；
/// 完成签名也问取值源。所以它（以及含有它的节点 sender、整个图）是依赖环境的 sender，无环境时报 dependent_sender_error。
///
///   hole<Dependency>               占位符
///   hole_count_v<Sender>           节点 sender 里占位符的个数。只递归穿过 stdexec 的算法层（then / let_value / when_all ……）；
///                                  其它 sender 都是不透明的叶子——包括嵌套的图：图 sender 不是 stdexec 的算法 sender，
///                                  它里面的占位符属于内层图，自然不会被数到。所以占位符不需要认识图
///   hole_filled_t<Sender, Input>   占位符换成 Input 之后的类型。沿 stdexec 的算法层重建，含占位符的子树递归替换，其余原样保留
///   fill_hole(sender, input)       同上，运行期构造
///
/// hole_count_v 与 hole_filled_t 有反射与模板两种实现，用 SDAG_REFLECTION 隔开。

#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/completion_behavior.hpp>
#include <exec/completion_signatures.hpp>

#include "../config.h"
#include "../general/type_list.h"
#include "../stdexec/expressions.h"
#include "../stdexec/dependent.h"
#include "value_source.h"

namespace sdag::protocol
{

/// 占位符（以及降解探针里的占位 sender）在 start 里同步完成。
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

/// 单独写成函数：get_completion_signatures 的 Env 是包，不能直接展开进只接受一个 Env 的别名模板。
template<class Dependency, class Env>
[[nodiscard]] consteval auto hole_completions()
{
	return source_completions_t<value_source_of_t<Env>, Dependency, Env>{};
}

template<class Dependency, class Receiver>
struct hole_operation
{
	Receiver _receiver;

	constexpr void start() noexcept
	{
		protocol::get_value_source(::stdexec::get_env(_receiver))->template deliver<Dependency>(::std::move(_receiver));
	}
};

}

template<class Dependency>
struct hole;

/// 是不是占位符。两种计算模式与运行期的 fill_hole 共用这一份。
namespace hole_traits
{

template<class T>
inline constexpr bool is_hole_v = false;

template<class Dependency>
inline constexpr bool is_hole_v<hole<Dependency>> = true;

}

template<class Dependency>
struct hole
{
	using sender_concept = ::stdexec::sender_t;
	using dependency = Dependency;

	[[nodiscard]] static constexpr hole_attrs get_env() noexcept
	{
		return {};
	}

	template<class Self, class... Env>
	[[nodiscard]] static consteval auto get_completion_signatures()
	{
		if constexpr (sizeof...(Env) == 0)
		{
			return stdexec_adapter::dependent_sender_error<Self>();
		}
		else if constexpr (!(has_value_source<Env> && ...))
		{
			return ::exec::throw_compile_time_error<_A_HOLE_WAS_CONNECTED_WITHOUT_A_VALUE_SOURCE_IN_ITS_ENVIRONMENT_>();
		}
		else
		{
			return detail::hole_completions<Dependency, Env...>();
		}
	}

	template<::stdexec::receiver Receiver>
	[[nodiscard]] constexpr detail::hole_operation<Dependency, Receiver> connect(Receiver receiver) const
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

/// hole_count 只穿过算法层去数占位符，所以这里遇到的含占位符的 sender 一定拆得开。
[[nodiscard]] consteval info hole_filled(info sender, info input)
{
	sender = plain(sender);
	if (specializes(sender, ^^hole))
	{
		return ::std::meta::decay(input);
	}
	if (hole_count(sender) == 0)
	{
		return sender;
	}
	infos const all = parts(sender);
	infos rebuilt{all[0], all[1]};
	for (::std::size_t index = 2; index < all.size(); ++index)
	{
		rebuilt.push_back(hole_filled(all[index], input));
	}
	return ::std::meta::substitute(^^stdexec_adapter::expression_t, rebuilt);
}

}

template<class Sender>
inline constexpr ::std::size_t hole_count_v = refl::hole_count(^^Sender);

template<class Sender, class Input>
using hole_filled_t = typename [: refl::hole_filled(^^Sender, ^^Input) :];

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

template<class Sender, class Input>
struct hole_filled;

template<class Parts, class Input>
struct rebuilt;

template<class Tag, class Data, class... Children, class Input>
struct rebuilt<general::type_list<Tag, Data, Children...>, Input>
{
	using type = stdexec_adapter::expression_t<Tag, Data, typename hole_filled<Children, Input>::type...>;
};

template<class Sender, class Input>
struct hole_filled
{
	using plain = ::std::remove_cvref_t<Sender>;

	static consteval auto compute() noexcept
	{
		if constexpr (hole_traits::is_hole_v<plain>)
		{
			return ::std::type_identity<::std::decay_t<Input>>{};
		}
		else if constexpr (hole_count<plain>() == 0)
		{
			return ::std::type_identity<plain>{};
		}
		else
		{
			return ::std::type_identity<typename rebuilt<stdexec_adapter::parts_t<plain>, Input>::type>{};
		}
	}

	using type = typename decltype(compute())::type;
};

}

template<class Sender>
inline constexpr ::std::size_t hole_count_v = detail::hole_count<Sender>();

template<class Sender, class Input>
using hole_filled_t = typename detail::hole_filled<Sender, Input>::type;

#endif

}

/// 把节点 sender 里的占位符换成 input（运行期）。类型与 hole_filled_t 一致；不含占位符的子树原样转发。
template<class Input, class CvSender>
[[nodiscard]] constexpr hole_filled_t<CvSender&&, Input> fill_hole(CvSender&& sender, Input&& input)
{
	using sender_type = ::std::remove_cvref_t<CvSender>;
	if constexpr (hole_traits::is_hole_v<sender_type>)
	{
		return ::std::forward<Input>(input);
	}
	else if constexpr (hole_count_v<sender_type> == 0)
	{
		return ::std::forward<CvSender>(sender);
	}
	else
	{
		return stdexec_adapter::apply(
			[&]<class Tag, class Data, class... Children>(Tag&& tag, Data&& data, Children&&... children)
			{
				// 节点只含一个占位符，所以只有一个子树会真正用到 input，它只会被移动一次。
				return stdexec_adapter::make_expression(::std::forward<Tag>(tag), ::std::forward<Data>(data),
					protocol::fill_hole(::std::forward<Children>(children), ::std::forward<Input>(input))...);
			},
			::std::forward<CvSender>(sender));
	}
}

}
