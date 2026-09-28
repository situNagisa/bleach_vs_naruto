#pragma once

#include <cassert>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "./concepts.h"
#include "./image_state.h"
#include "./mock_vulkan.h"

/// domain、pending_value、tracked_value：默认实现，让 tracked_resource/pending_resource
/// 这两个 concept 落地成能真的跑起来的东西。
///
/// domain 只做一件事——"当前在写哪个命令缓冲、结束这一批时要提交给谁"。它不做任何
/// 资源状态判断：哪里要不要同步，已经由 transite/consume_external 在编译期声明好了，
/// domain 只管照办、记账"批次"这一层。这不是最终形态：以后要不要换成专门的录制域
/// scheduler（对应 nvexec 的 stream_scheduler 那种 domain 改写），是另一个可以单独
/// 决定的架构问题；这里先用一个能被 env 查到的引用作为占位实现，把"批次由谁负责"
/// 和"原语/concept 的形状"这两个问题分开。
namespace gpu::mock_domain
{
class domain
{
public:
	[[nodiscard]] ::gpu::mock::command_buffer_handle command_buffer() const noexcept { return _command; }

	void add_wait(::gpu::mock::signal_handle signal) { _waits.push_back(signal); }

	/// 结束当前命令缓冲、提交、拿到这批的信号，开一个新的命令缓冲接着录。
	[[nodiscard]] ::gpu::mock::signal_handle cut()
	{
		auto const signal = ::gpu::mock::queue_submit(_command, _waits);
		_waits.clear();
		_command = ::gpu::mock::next_handle();
		return signal;
	}

private:
	::gpu::mock::command_buffer_handle _command = ::gpu::mock::next_handle();
	::std::vector<::gpu::mock::signal_handle> _waits{};
};

/// 给 transite/consume_external 的实现用的环境查询：这一步是不是还在某个 domain 的
/// 录制链里。查不到（返回 nullptr）就意味着"这里已经是 CPU 观察点"。
///
/// 普通用户代码（比如 main.cpp 里自己写的 clear 那个 then）不需要这个查询——直接
/// 按平常写法捕获自己手上的 domain 引用就够了；只有像 transite 这种写一次、被所有
/// 资源类型复用、不知道调用者具体是谁的实现，才需要靠环境去问"我现在录在哪"。
///
/// 必须继承 forwarding_query_t：env 在跨调度器边界组合时（这里是 sync_wait 的
/// run_loop scheduler 那一层）会把外层 env 包进一个只转发"转发查询"的 __fwd
/// 包装里，不满足 forwarding_query 的查询会在那一层被直接丢弃、查不到——这不是
/// 我们自己另开的一套机制，是 stdexec 环境组合本身的规则，跟 get_stop_token 这类
/// 标准查询要穿过同样的边界是同一个道理。
///
/// 检测返回类型时用 convertible_to 而不是 same_as：`stdexec::prop` 的 query()
/// 按 `Value const&` 返回（这里 Value 是 domain*），same_as<domain*> 对着一个
/// 引用类型必然是 false，会让这条检测永远走不到"找到了"这一支，看起来像是查
/// 不到，其实是约束本身写错了。
struct get_domain_t : ::stdexec::forwarding_query_t
{
	template <class Env>
	domain* operator()(Env const& env) const noexcept
	{
		if constexpr (requires { { env.query(get_domain_t{}) } -> ::std::convertible_to<domain*>; })
			return env.query(*this);
		else
			return nullptr;
	}
};
inline constexpr get_domain_t get_domain{};

template <class Handle, class State>
struct tracked_value;

/// consume_external 的实现：如果下游环境里还挂着一个 domain（还在往某个命令缓冲里
/// 录），就把这个信号记成"这一批要等的东西"——对应 CUDA 的 cudaStreamWaitEvent；
/// 如果没有（下游已经是纯 CPU 观察点），就真的等它——对应 cudaEventSynchronize。
/// 是哪一种取决于谁在消费这个值，不取决于值本身，跟 CUDA 的 event 是同一个道理。
template <class Handle, class State>
struct consume_external_sender
{
	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = ::stdexec::completion_signatures<::stdexec::set_value_t(tracked_value<Handle, State>)>;

	Handle _handle;
	State _state;
	::gpu::mock::signal_handle _signal;

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		Handle _handle;
		State _state;
		::gpu::mock::signal_handle _signal;
		Receiver _receiver;

		void start() & noexcept
		{
			if (auto* dom = ::gpu::get_domain(::stdexec::get_env(_receiver)))
				dom->add_wait(_signal);
			else
				::gpu::mock::wait_signal(_signal);
			::stdexec::set_value(::std::move(_receiver), tracked_value<Handle, State>{_handle, _state});
		}
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const
	{
		return {_handle, _state, _signal, ::std::move(receiver)};
	}
};

/// submit 产出的通用 pending 类型：句柄 + 状态 + 这批提交出来的信号。对任何满足
/// tracked_resource 的 T 都适用（见 algorithms.h 的 submit），不需要再多一个 CPO
/// 去问"你的 pending 形态长什么样"——直接复用 resource/state 这两个已有原语构造。
template <class Handle, class State>
struct pending_value
{
	Handle _handle;
	State _state;
	::gpu::mock::signal_handle _signal;

	[[nodiscard]] consume_external_sender<Handle, State> consume_external() const
	{
		return {_handle, _state, _signal};
	}
};

/// make_pending 的 ADL 实现：algorithms.h::submit 靠这个把"提交完的 tracked_resource
/// + 这批的信号"包成本命名空间的 pending_value，不需要 submit 认识这个类型模板。
template <class Handle, class State>
[[nodiscard]] pending_value<Handle, State> make_pending(tracked_value<Handle, State> const& item, ::gpu::mock::signal_handle signal)
{
	return {::gpu::resource(item), ::gpu::state(item), signal};
}

/// transite 的实现：按 From/To 是否同属 image_state 或 buffer_state 分支，
/// 只有这一处需要知道"这是不是一张图"，image 和 buffer 共用同一个 tracked_value，
/// 不需要为它们分别定义类型。
template <class Handle, class From, class To>
struct transite_sender
{
	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = ::stdexec::completion_signatures<::stdexec::set_value_t(tracked_value<Handle, To>)>;

	Handle _handle;
	From _from;
	To _to;

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		Handle _handle;
		From _from;
		To _to;
		Receiver _receiver;

		void start() & noexcept
		{
			auto* dom = ::gpu::get_domain(::stdexec::get_env(_receiver));
			assert(dom != nullptr && "transite: 只能在还有 domain 在录制的链路里调用");
			if constexpr (image_state<From> && image_state<To>)
			{
				mock::cmd_pipeline_barrier(dom->command_buffer(),
					_from.stage(), _from.access(), _to.stage(), _to.access(),
					_from.layout(), _to.layout(), _handle);
			}
			else if constexpr (buffer_state<From> && buffer_state<To>)
			{
				mock::cmd_buffer_barrier(dom->command_buffer(),
					_from.stage(), _from.access(), _to.stage(), _to.access(), _handle);
			}
			else
			{
				static_assert(detail::always_false<To>, "transite: From/To 必须同属 image_state 或同属 buffer_state");
			}
			::stdexec::set_value(::std::move(_receiver), tracked_value<Handle, To>{_handle, _to});
		}
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const
	{
		return {_handle, _from, _to, ::std::move(receiver)};
	}
};

/// tracked_resource 的默认实现：resource()/state() 是纯字段读取；transite(to)
/// 转发到 transite_sender，真正的分支逻辑在那边。
template <class Handle, class State>
struct tracked_value
{
	Handle _handle;
	State _state;

	[[nodiscard]] Handle resource() const noexcept { return _handle; }
	[[nodiscard]] State state() const noexcept { return _state; }

	template <class To>
	[[nodiscard]] transite_sender<Handle, State, To> transite(To to) const
	{
		return {_handle, _state, ::std::move(to)};
	}
};

static_assert(tracked_resource<tracked_value<mock::image_handle, undefined>>);
static_assert(pending_resource<pending_value<mock::image_handle, undefined>>);
}
