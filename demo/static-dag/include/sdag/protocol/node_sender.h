#pragma once

/// 节点变 sender：把占位符喂给节点载荷得到节点 sender，并检查占位符恰好用一次。依赖：前驱占位符、节点。
///
///   make_node_sender(node)      节点 → 节点 sender
///   node_sender_of_t<Node>      它的类型

#include <type_traits>
#include <utility>

#include "../node/node.h"
#include "hole.h"

namespace sdag::protocol
{

/// 节点的 sender：载荷是 sender 就原样取出（控制依赖）；是适配器就把前驱占位符喂给它（数据依赖）。
template<class Node>
[[nodiscard]] constexpr auto make_node_sender(Node&& node)
{
	using dependency = node_dependencies_t<Node>;
	using payload_type = decltype(::sdag::payload(::std::forward<Node>(node)));
	static_assert(node_payload<payload_type, protocol::hole<dependency>>,
		"sdag: 节点载荷既不是 sender，也不是能接受前驱占位符的适配器（例如 ::stdexec::then(f)）");
	if constexpr (sender_payload<payload_type>)
	{
		static_assert(protocol::hole_count_v<::std::decay_t<payload_type>> == 0, "sdag: 只有控制依赖的节点 sender 里不能出现前驱占位符");
		return ::std::decay_t<payload_type>(::sdag::payload(::std::forward<Node>(node)));
	}
	else
	{
		using child = decltype(::sdag::payload(::std::forward<Node>(node))(protocol::hole<dependency>{}));
		static_assert(protocol::hole_count_v<child> == 1,
			"sdag: 节点适配器必须把前驱占位符恰好用一次，而且中间只能隔着 stdexec 的算法层："
			"它的值只会被交付一次，repeat / retry 这类会重启子 sender 的适配器不能套在输入上；"
			"不透明的自定义 sender 包着占位符时这里会数成 0 次");
		return ::sdag::payload(::std::forward<Node>(node))(protocol::hole<dependency>{});
	}
}

template<class Node>
using node_sender_of_t = decltype(protocol::make_node_sender(::std::declval<Node>()));

}
