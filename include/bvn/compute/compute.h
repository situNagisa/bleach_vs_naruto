#pragma once

#include <nagisa/concurrency/concurrency.h>
#include <nagisa/concurrency/simple_task.h>

namespace bvn::compute
{
	namespace nc = ::nagisa::concurrency;

	inline auto smoke_task() -> nc::simple_task<void>
	{
		co_return;
	}
}
