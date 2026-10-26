#pragma once

/// 重建 stdexec 的算法 sender：由 [tag, data, child...] 拼回一个 sender 表达式。依赖：sdag 核心的 stdexec/expressions.h（拆开）。
///
/// 这是 sdag-compose 对 stdexec 内部实现的全部依赖：标准里对应的 make-sender 只是说明用的，没有公开接口，所以用 __sexpr。
/// 只有降解需要它——降解要把节点 sender 里的占位符换成 fanin(...)，拆开、替换子 sender、再拼回去。

#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include <sdag/stdexec/expressions.h>

namespace sdag::stdexec_adapter
{

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

}
