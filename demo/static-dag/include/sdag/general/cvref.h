#pragma once

/// cv/引用转抄：让一个类型带上另一个类型的 const 与值类别。

#include <utility>

namespace sdag::general
{

/// To 带上 From 的 const 与值类别（std::forward_like 的类型版）。
template<class From, class To>
using copy_cvref_t = decltype(::std::forward_like<From>(::std::declval<To&>()));

}
