#pragma once

/// 取值源协议：节点到哪里取前驱的值。
///
/// 节点 sender 里的占位符（protocol/hole.h）自己不存值，也不认识图。它连接时拿到的接收者环境里有一个转发查询
/// get_value_source，返回一个指向"取值源"的指针。取值源实现两个接口：
///
///   签名接口   template<size_t Node, class Env> using completions_for = ...;
///              第 Node 个节点的 let / move 前驱的值，在环境 Env 下交给接收者时的完成签名
///   交付接口   template<size_t Node, class Receiver> void deliver(Receiver&&) noexcept;
///              把这些值交给接收者（一次 set_value）
///
/// 图这一侧由整图状态（graph/slots.h）实现交付接口、由前驱的签名（graph/pred_sigs.h）实现签名接口。
/// 占位符只依赖本文件，所以"节点"与"图"之间没有环：图认识占位符，占位符只认识协议。
///
///   get_value_source            环境里的查询（转发查询：then / let_value / continues_on 等适配器包装环境时原样转发）
///   has_value_source<Env>       环境里有没有取值源
///   value_source_of_t<Env>      取值源的类型
///   value_source_traits<Source> 签名接口的定制点（默认转到 Source::completions_for）
///   source_completions_t<Source, Node, Env>   签名接口

#include <cstddef>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

namespace sdag::protocol
{

struct get_value_source_t : ::stdexec::forwarding_query_t
{
	template<class Env>
		requires requires(Env const& env) { env.query(::std::declval<get_value_source_t>()); }
	[[nodiscard]] static constexpr auto operator()(Env const& env) noexcept
	{
		return env.query(get_value_source_t{});
	}
};

inline constexpr get_value_source_t get_value_source{};

template<class Env>
concept has_value_source = requires(Env const& env) { protocol::get_value_source(env); };

template<class Env>
	requires has_value_source<Env>
using value_source_of_t = ::std::remove_pointer_t<decltype(protocol::get_value_source(::std::declval<Env const&>()))>;

/// 签名接口的入口。默认取 Source 的成员 completions_for；取值源也可以特化本模板，在类外提供签名接口——
/// 签名是在取值源自身还不完整的时候算的（整图状态的布局依赖节点签名，节点签名又要问取值源），
/// 这时读 Source 的成员会实例化它、成环；特化的匹配只看类型，不实例化。
template<class Source>
struct value_source_traits
{
	template<::std::size_t Node, class Env>
	using completions_for = typename Source::template completions_for<Node, Env>;
};

template<class Source, ::std::size_t Node, class Env>
using source_completions_t = typename value_source_traits<Source>::template completions_for<Node, Env>;

}
