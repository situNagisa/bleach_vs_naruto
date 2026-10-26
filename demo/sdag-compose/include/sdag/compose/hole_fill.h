#pragma once

/// 换掉占位符：把节点 sender 里的前驱占位符换成真正的输入 sender。依赖：sdag 核心的前驱占位符、类型列表；sender 表达式重建。
///
///   hole_filled_t<Sender, Input>   占位符换成 Input 之后的类型。沿 stdexec 的算法层重建，含占位符的子树递归替换，其余原样保留
///   fill_hole(sender, input)       同上，运行期构造
///
/// hole_filled_t 有反射与模板两种实现，用 SDAG_REFLECTION 隔开。

#include <cstddef>
#include <type_traits>
#include <utility>

#include <sdag/config.h>
#include <sdag/general/type_list.h>
#include <sdag/protocol/hole.h>

#include "expressions.h"

namespace sdag::compose
{

inline namespace SDAG_ABI_NAMESPACE
{

#if SDAG_REFLECTION

namespace refl
{

using namespace ::sdag::general::refl;

/// hole_count 只穿过算法层去数占位符，所以这里遇到的含占位符的 sender 一定拆得开。
[[nodiscard]] consteval info hole_filled(info sender, info input)
{
	sender = plain(sender);
	if (specializes(sender, ^^protocol::hole))
	{
		return ::std::meta::decay(input);
	}
	if (protocol::refl::hole_count(sender) == 0)
	{
		return sender;
	}
	infos const all = protocol::refl::parts(sender);
	infos rebuilt{all[0], all[1]};
	for (::std::size_t index = 2; index < all.size(); ++index)
	{
		rebuilt.push_back(hole_filled(all[index], input));
	}
	return ::std::meta::substitute(^^stdexec_adapter::expression_t, rebuilt);
}

}

template<class Sender, class Input>
using hole_filled_t = typename [: refl::hole_filled(^^Sender, ^^Input) :];

#else

namespace detail
{

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
		if constexpr (protocol::hole_traits::is_hole_v<plain>)
		{
			return ::std::type_identity<::std::decay_t<Input>>{};
		}
		else if constexpr (protocol::hole_count_v<plain> == 0)
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

template<class Sender, class Input>
using hole_filled_t = typename detail::hole_filled<Sender, Input>::type;

#endif

}

/// 把节点 sender 里的占位符换成 input（运行期）。类型与 hole_filled_t 一致；不含占位符的子树原样转发。
template<class Input, class CvSender>
[[nodiscard]] constexpr hole_filled_t<CvSender&&, Input> fill_hole(CvSender&& sender, Input&& input)
{
	using sender_type = ::std::remove_cvref_t<CvSender>;
	if constexpr (protocol::hole_traits::is_hole_v<sender_type>)
	{
		return ::std::forward<Input>(input);
	}
	else if constexpr (protocol::hole_count_v<sender_type> == 0)
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
					compose::fill_hole(::std::forward<Children>(children), ::std::forward<Input>(input))...);
			},
			::std::forward<CvSender>(sender));
	}
}

}
