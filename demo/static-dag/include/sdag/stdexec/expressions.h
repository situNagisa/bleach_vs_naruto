#pragma once

/// sender 表达式：拆开 stdexec 的算法 sender。
///
/// 这里是 sdag 核心对 stdexec 内部实现（带双下划线的名字）的全部依赖。除此之外核心只用 stdexec / exec 的公开接口。
///
/// 为什么需要它：数节点 sender 里的占位符（protocol/hole.h）要把 then(占位符, f) 这样的 stdexec 算法 sender
/// 拆成 [tag, data, child...]。
///
///   读   P2300 说 sender 表达式可以结构化绑定成 [tag, data, child...]，但 stdexec 的 __tuple 超过 8 个元素
///        就不能结构化绑定了（when_all 有 7 个子 sender 时就是这样），所以直接用它的内部别名 __tag_t /
///        __data_t / __children_t。只认 stdexec 自己的算法 sender：自定义 sender 和 sdag 的图 sender
///        都不满足 sender_expression，被当作不透明的叶子。

#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "../general/type_list.h"

namespace sdag::stdexec_adapter
{

// ---------------------------------------------------------------- 读

/// stdexec 的算法 sender（__sexpr 及其派生类型）。
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
	using type = general::type_list<Tag, Data, Children...>;
};

/// type_list<Tag, Data, Child...>（去掉了 cv / 引用）。
template<sender_expression Sender>
using parts_t = typename parts<
	typename ::std::remove_cvref_t<Sender>::__tag_t,
	::std::remove_cvref_t<typename ::std::remove_cvref_t<Sender>::__data_t>,
	typename ::std::remove_cvref_t<Sender>::__children_t>::type;

}
