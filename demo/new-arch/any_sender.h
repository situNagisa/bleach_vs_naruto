#pragma once

#include <stdexcept>

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>

using any_receiver_type = ::exec::any_receiver<
	::stdexec::completion_signatures<
	::stdexec::set_value_t(),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>,
	::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept>>;
using any_sender_type = ::exec::any_sender<any_receiver_type>;
using any_scheduler_type = ::exec::any_scheduler<any_sender_type>;