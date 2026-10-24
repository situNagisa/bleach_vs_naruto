module;

// 这是整个 demo 里唯一允许直接看到 stdexec 头文件的模块接口单元。
// 其他模块只 import demo.execution，不再各自重新解析 stdexec。
// 声明的唯一来源是 execution.h（README §3）；这里只提供依赖、导出声明，并重导出 stdexec 符号。
#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/start_detached.hpp>

export module demo.execution;

// export extern "C++"：声明附着到全局模块，与头文件模式下的同名声明是同一实体。
export extern "C++"
{
#define BVN_EXEC0_MODULE_INTERFACE
#include "execution.h"
#undef BVN_EXEC0_MODULE_INTERFACE
}

// stdexec / exec 的符号在各自的命名空间里重新导出：下游必须写 ::stdexec::just、::exec::static_thread_pool，
// 不允许出现无限定的 just / then。需要新的算法时在这里追加（头文件模式直接包含 stdexec 头，无需重导出）。
export namespace stdexec
{
using ::stdexec::just;
using ::stdexec::schedule;
using ::stdexec::then;
using ::stdexec::let_value;
using ::stdexec::when_all;
using ::stdexec::starts_on;
using ::stdexec::continues_on;
using ::stdexec::sync_wait;
}

// stdexec 里 exec 只是 experimental::execution 的命名空间别名，别名本身也要导出，下游才能写 ::exec::。
export namespace experimental::execution
{
using ::experimental::execution::static_thread_pool;
using ::experimental::execution::start_detached;
}
export namespace exec = ::experimental::execution;
