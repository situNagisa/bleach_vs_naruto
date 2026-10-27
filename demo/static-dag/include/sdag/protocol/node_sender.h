#pragma once

/// 节点变 sender：把占位符喂给节点载荷得到节点 sender，并检查占位符恰好用一次。
///
///   make_node_sender<I, HasValues>(node)   第 I 个节点 → 节点 sender（HasValues：有没有 let / move 依赖）

#include <cstddef>
#include <type_traits>
#include <utility>

#include "../node/node.h"
#include "hole.h"

namespace sdag::protocol
{

/// 节点的 sender：没有 let / move 依赖时载荷就是 sender，原样取出；有时载荷是适配器，把前驱占位符 hole<I> 喂给它。
template<::std::size_t I, bool HasValues, class Node>
[[nodiscard]] constexpr auto make_node_sender(Node&& node)
{
	using payload_type = decltype((::std::forward<Node>(node)._payload));
	if constexpr (!HasValues)
	{
		static_assert(sender_payload<::std::decay_t<payload_type>>,
			"sdag: 没有 let / move 依赖的节点，载荷必须是 sender；要接收前驱的值请用 let 或 move");
		static_assert(protocol::hole_count_v<::std::decay_t<payload_type>> == 0, "sdag: 没有 let / move 依赖的节点 sender 里不能出现前驱占位符");
		return ::std::decay_t<payload_type>(::std::forward<Node>(node)._payload);
	}
	else
	{
		static_assert(adaptor_payload<payload_type, protocol::hole<I>>,
			"sdag: 有 let / move 依赖的节点，载荷必须是能接受前驱占位符的 sender 适配器（例如 ::stdexec::then(f)）；"
			"只想等先后、不要值，请用 wait");
		using child = decltype(::std::forward<Node>(node)._payload(protocol::hole<I>{}));
		static_assert(protocol::hole_count_v<child> == 1,
			"sdag: 节点适配器必须把前驱占位符恰好用一次，而且中间只能隔着 stdexec 的算法层："
			"它的值只会被交付一次，repeat / retry 这类会重启子 sender 的适配器不能套在输入上；"
			"不透明的自定义 sender 包着占位符时这里会数成 0 次");
		return ::std::forward<Node>(node)._payload(protocol::hole<I>{});
	}
}

}
