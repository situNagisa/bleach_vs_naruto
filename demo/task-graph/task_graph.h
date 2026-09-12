#pragma once

/// 每帧重建的任务图：先 add_job 注册所有类型，再 run_frame 构建并执行。
/// 每帧每种 job 唯一，依赖通过 context.job<T>() 获取托管对象的可选引用。
/// 调度器和帧数据由使用者的派生上下文提供，取消由节点自行注入。

#include <exception>
#include <semaphore>
#include <utility>

#include <stdexec/execution.hpp>

#include "node_sender.h"
#include "dynamic_when_all.h"
#include "job.h"
#include "frame_context.h"

namespace bvn::task_graph
{

/// 阶段 B + 启动 + 等完成。返回 false 表示整帧被取消；图内的错误以异常抛出。
///
/// 这里手写了一个 `sync_wait`，只为了能把结构令牌塞进根接收者的环境——
/// 取消必须由节点自己 `write_env` 注入，不能从结构边压下来。
inline auto run_frame(frame_context& context, ::stdexec::inplace_stop_token stop_token = {}) -> bool
{
	struct forward_stop
	{
		::stdexec::inplace_stop_source* _target;

		auto operator()() const noexcept -> void
		{
			_target->request_stop();
		}
	};

	struct run_state
	{
		::std::binary_semaphore _done{0};
		::std::exception_ptr _error;
		bool _stopped = false;
		::stdexec::inplace_stop_token _stop_token;
	};

	struct run_receiver
	{
		using receiver_concept = ::stdexec::receiver_t;

		run_state* _state;

		auto set_value() noexcept -> void
		{
			_state->_done.release();
		}

		auto set_error(::std::exception_ptr error) noexcept -> void
		{
			_state->_error = ::std::move(error);
			_state->_done.release();
		}

		auto set_stopped() noexcept -> void
		{
			_state->_stopped = true;
			_state->_done.release();
		}

		[[nodiscard]] auto get_env() const noexcept -> node_env
		{
			return node_env{._stop_token = _state->_stop_token};
		}
	};

	// 外部取消转发进本帧的取消源。已经停止的令牌在这里注册即刻回调，
	// 于是图还没启动就已经是"取消态"了。
	auto const forward = ::stdexec::inplace_stop_callback<forward_stop>{stop_token, forward_stop{&context._stop_source}};

	// 阶段 B：每个 job 自己往上下文挂。这里不问、不收、不排序。
	for (auto&& job : context._jobs)
	{
		job->build();
	}

	auto state = run_state{._error = {}, ._stop_token = context._structural_source.get_token()};
	auto operation = ::stdexec::connect(
		::bvn::task_graph::dynamic_when_all(::std::move(context._roots)), run_receiver{&state});
	::stdexec::start(operation);
	state._done.acquire();

	if (state._error)
	{
		::std::rethrow_exception(state._error);
	}

	return !state._stopped;
}

}
