#pragma once

/// 依赖型报错：「没有环境，算不出签名」怎么向 stdexec 报告。依赖：无。
///
/// 这是 sdag 对 stdexec 内部实现的两处依赖之一（另一处是 stdexec/expressions.h）。

#include <stdexec/execution.hpp>

namespace sdag::stdexec_adapter
{

/// get_completion_signatures 在没有环境、算不出签名时的标准答复（P3557 的 dependent_sender_error）。
/// stdexec 按是否支持 constexpr 异常在"返回"与"抛出"之间切换，这个细节只有它自己知道。
template<class Sender>
[[nodiscard]] consteval auto dependent_sender_error()
{
	return ::stdexec::__throw_dependent_sender_error<Sender>();
}

}
