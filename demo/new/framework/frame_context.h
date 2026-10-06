#pragma once

#include <vector>
#include <cstdint>

#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/any_sender_of.hpp>

#include "./any_sender.h"

struct frame_context
{
	::std::uint64_t index;
	::exec::static_thread_pool::scheduler scheduler;
	::stdexec::inplace_stop_token stop_token;
	::std::vector<any_sender_type> roots;
};