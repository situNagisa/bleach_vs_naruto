// demo.execution 的声明，唯一来源。按宏展开成三种形态（README §3）：
//   模块接口单元内（定义 BVN_EXEC0_MODULE_INTERFACE）：只有声明，依赖由 execution.cppm 的全局模块片段提供；
//   模块模式的使用方（只定义 BVN_EXEC0_MODULES）：只 import，不出现声明；
//   头文件模式（都不定义，VS / IntelliSense 永远是这种）：#include 依赖，再给出声明。
// stdexec 符号的重导出（export namespace stdexec { using ... }）只在 execution.cppm 里；
// 头文件模式直接包含了 stdexec 头，::stdexec::just 等本来就可见。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.execution;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <exception>

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/start_detached.hpp>
#endif

#ifndef BVN_EXEC0_EXECUTION_EXECUTION_H
#define BVN_EXEC0_EXECUTION_EXECUTION_H

template<class... Vs>
using values_receiver_type = ::exec::any_receiver<
	::stdexec::completion_signatures<
	::stdexec::set_value_t(Vs...),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>,
	::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept>>;

template<class... Vs>
using values_sender_type = ::exec::any_sender<values_receiver_type<Vs...>>;

using any_scheduler_type = ::exec::any_scheduler<values_sender_type<>>;

#endif

#endif
