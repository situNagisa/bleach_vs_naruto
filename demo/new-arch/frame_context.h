#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/any_sender_of.hpp>

using any_receiver_type = ::exec::any_receiver<
	::stdexec::completion_signatures<
	::stdexec::set_value_t(),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>,
	::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept>>;
using any_sender_type = ::exec::any_sender<any_receiver_type>;

struct frame_context
{
	::std::uint64_t index;
	::exec::static_thread_pool::scheduler scheduler;
	::stdexec::inplace_stop_token stop_token;
	::std::vector<any_sender_type> roots;
};