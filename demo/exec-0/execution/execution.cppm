module;

// 这是整个 demo 里唯一允许直接看到 stdexec 头文件的模块接口单元。
// 其他模块只 import demo.execution，不再各自重新解析 stdexec。
#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/start_detached.hpp>
#include <exec/finally.hpp>
// nagisa 的 semaphore 也基于 stdexec，同样只在这里包含：多个接口单元在全局模块片段各自包含它时，
// 导入方合并声明会出错（clang 22 报 operator new 二义性 / 序列化崩溃）。
#include <nagisa/concurrency/semaphore.h>
#include <nagisa/concurrency/coroutine.h>
#include <nagisa/concurrency/simple_task.h>
// sdag（静态 DAG sender）同理：它依赖 stdexec，只在这里包含，下游写 ::sdag::dag / ::sdag::make_node / ::sdag::data / ::sdag::control。
#include <sdag/sdag.h>

export module demo.execution;

export template<class... Vs>
using values_receiver_type = ::exec::any_receiver<
	::stdexec::completion_signatures<
	::stdexec::set_value_t(Vs...),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>,
	::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept>>;

export template<class... Vs>
using values_sender_type = ::exec::any_sender<values_receiver_type<Vs...>>;

export using any_scheduler_type = ::exec::any_scheduler<values_sender_type<>>;

// stdexec / exec 的符号在各自的命名空间里重新导出：下游必须写 ::stdexec::just、::exec::static_thread_pool，
// 不允许出现无限定的 just / then。需要新的算法时在这里追加。
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
using ::stdexec::run_loop;
using ::stdexec::read_env;
using ::stdexec::write_env;
using ::stdexec::get_allocator;
using ::stdexec::get_stop_token;
using ::stdexec::get_scheduler;
using ::stdexec::get_start_scheduler;
using ::stdexec::get_completion_scheduler;
using ::stdexec::inplace_stop_source;
using ::stdexec::inplace_stop_token;
using ::stdexec::counting_scope;
using ::stdexec::simple_counting_scope;
using ::stdexec::spawn;
using ::stdexec::upon_error;
}

// stdexec 里 exec 只是 experimental::execution 的命名空间别名，别名本身也要导出，下游才能写 ::exec::。
export namespace experimental::execution
{
using ::experimental::execution::static_thread_pool;
using ::experimental::execution::bwos_params;
using ::experimental::execution::start_detached;
using ::experimental::execution::finally;
}
export namespace exec = ::experimental::execution;

export namespace nagisa::concurrency
{
using ::nagisa::concurrency::binary_semaphore_guard;
using ::nagisa::concurrency::bounded_resource_pool;
using ::nagisa::concurrency::resource_pool;
using ::nagisa::concurrency::simple_task;
}

export namespace sdag
{
using ::sdag::dag;
using ::sdag::make_node;
using ::sdag::data;
using ::sdag::control;
using ::sdag::take;
}
