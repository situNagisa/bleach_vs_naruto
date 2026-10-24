#pragma once

/// 完成状态机：运行中 / 错误 / 停止；第一个错误胜出；外层停止请求怎么传。依赖：类型列表。
///
///   state_kind      running → error | stopped。只从 running 出发：第一个错误或停止把状态切走，之后的都不再改变它，
///                   但停止之后来的错误仍要保存（错误优先于停止）
///   state_cell      原子的 state_kind
///   counter<N>      初值为 N 的原子倒计数（入度计数、汇点计数）
///   error_variant_t<Errors>   std::variant<monostate, 错误类型...>；错误类型本身也可能是 monostate，所以一律按下标访问：错误 k 在第 k + 1 位

#include <atomic>
#include <cstddef>
#include <variant>

#include "../general/type_list.h"

namespace sdag::execution
{

enum class state_kind : unsigned char
{
	running,
	error,
	stopped,
};

struct state_cell
{
	::std::atomic<state_kind> value{state_kind::running};
};

template<::std::size_t Initial>
struct counter
{
	::std::atomic<::std::size_t> value{Initial};
};

template<class Errors>
using error_variant_t = general::rebind_t<::std::variant, general::concat_t<general::type_list<::std::monostate>, Errors>>;

}
