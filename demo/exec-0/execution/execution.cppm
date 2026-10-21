module;

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>
#include <exec/static_thread_pool.hpp>

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

export using ::exec::static_thread_pool;
export using ::stdexec::just;
export using ::stdexec::then;
