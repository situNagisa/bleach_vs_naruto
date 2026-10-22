#pragma once

/// stdexec 的边界：sdag 对 stdexec 内部实现（带双下划线的名字）的全部依赖都在这个文件里。
///
/// 除此之外，sdag 只用 stdexec / exec 的公开接口：sender / receiver 协议、completion_signatures、
/// 查询（get_env / get_stop_token / get_domain / get_completion_domain）、when_all / split / let_value 等算法、
/// exec::completion_behavior、exec::sender_for、exec::throw_compile_time_error。
///
/// 这里分两部分：
///   1. 读：stdexec 的算法 sender（then / let_value / continues_on / when_all ……）的 [tag, data, child...]。
///      P2300 说 sender 表达式可以结构化绑定成这个形状，但 stdexec 的 __tuple 超过 8 个元素就不能结构化绑定了
///      （when_all 有 7 个子 sender 时就是这样），所以这里直接用它的内部别名与 __apply。
///   2. 写：重建 sender 表达式、依赖型 sender 的报错。
/// 标准给出公开接口、或者 stdexec 升级时，只需要改这个文件。

#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "meta.h"

namespace sdag::bridge
{

// ---------------------------------------------------------------- 1. 读：sender 表达式的结构

/// stdexec 的算法 sender（__sexpr 及其派生类型）。自定义 sender、sdag 的图 sender 都不算。
template<class Sender>
concept sender_expression = requires {
	typename ::std::remove_cvref_t<Sender>::__tag_t;
	typename ::std::remove_cvref_t<Sender>::__data_t;
	typename ::std::remove_cvref_t<Sender>::__children_t;
};

template<class Tag, class Data, class Children>
struct parts;

template<class Tag, class Data, class... Children>
struct parts<Tag, Data, ::stdexec::__mlist<Children...>>
{
	using type = meta::type_list<Tag, Data, Children...>;
};

/// type_list<Tag, Data, Child...>（去掉了 cv / 引用）。
template<sender_expression Sender>
using parts_t = typename parts<
	typename ::std::remove_cvref_t<Sender>::__tag_t,
	::std::remove_cvref_t<typename ::std::remove_cvref_t<Sender>::__data_t>,
	typename ::std::remove_cvref_t<Sender>::__children_t>::type;

/// 把 sender 表达式拆开喂给 fn(tag, data, child...)，元素的值类别跟随 sender。
template<class Fn, sender_expression Sender>
constexpr decltype(auto) apply(Fn&& fn, Sender&& sender)
{
	return ::stdexec::__apply(::std::forward<Fn>(fn), ::std::forward<Sender>(sender));
}

// ---------------------------------------------------------------- 2. 写

/// 由 [tag, data, child...] 重建 sender 表达式。标准里对应的是仅供说明的 make-sender，没有公开接口；
/// stdexec 的算法 sender 都是 __sexpr<Tag, Data, Child...> 的（空）派生类，行为全在 __sexpr_impl<Tag> 里，
/// 所以重建出的裸 __sexpr 与原来的派生类型行为一致。
template<class Tag, class Data, class... Children>
using expression_t = ::stdexec::__sexpr<Tag, Data, Children...>;

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces" // __sexpr 的聚合初始化靠大括号省略，stdexec 自己也这样写
#endif
template<class Tag, class Data, class... Children>
[[nodiscard]] constexpr expression_t<::std::decay_t<Tag>, ::std::decay_t<Data>, ::std::decay_t<Children>...>
make_expression(Tag&& tag, Data&& data, Children&&... children)
{
	return ::stdexec::__sexpr{::std::forward<Tag>(tag), ::std::forward<Data>(data), ::std::forward<Children>(children)...};
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/// get_completion_signatures 在没有环境、算不出签名时的标准答复（P3557 的 dependent_sender_error）。
/// stdexec 按是否支持 constexpr 异常在"返回"与"抛出"之间切换，这个细节只有它自己知道。
template<class Sender>
[[nodiscard]] consteval auto dependent_sender_error()
{
	return ::stdexec::__throw_dependent_sender_error<Sender>();
}

}
