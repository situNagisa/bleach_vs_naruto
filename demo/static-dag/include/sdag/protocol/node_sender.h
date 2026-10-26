#pragma once

/// 节点变 sender：把占位符喂给节点载荷得到节点 sender，并检查占位符恰好用一次。
///
///   make_node_sender(node)      节点 → 节点 sender
///   node_sender_of_t<Node>      它的类型

#include <type_traits>
#include <utility>

#include "../node/node.h"
#include "hole.h"

namespace sdag::protocol
{

/// 节点的 sender：没有数据依赖时载荷就是 sender，原样取出；有数据依赖时载荷是适配器，把前驱占位符 hole<data<...>> 喂给它。
template<class Node>
[[nodiscard]] constexpr auto make_node_sender(Node&& node)
{
	using dependencies = node_dependencies_t<Node>;
	using dependency = typename dependencies::data_type;
	using payload_type = decltype(::sdag::payload(::std::forward<Node>(node)));
	if constexpr (!dependencies::has_data)
	{
		static_assert(sender_payload<payload_type>,
			"sdag: 没有数据依赖（data<...>）的节点，载荷必须是 sender；要接收前驱的值请把前驱写进 data<...>");
		static_assert(protocol::hole_count_v<::std::decay_t<payload_type>> == 0, "sdag: 没有数据依赖的节点 sender 里不能出现前驱占位符");
		return ::std::decay_t<payload_type>(::sdag::payload(::std::forward<Node>(node)));
	}
	else
	{
		static_assert(adaptor_payload<payload_type, protocol::hole<dependency>>,
			"sdag: 有数据依赖（data<...>）的节点，载荷必须是能接受前驱占位符的 sender 适配器（例如 ::stdexec::then(f)）；"
			"只想等先后、不要值，请把前驱写进 control<...>");
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
