#pragma once

#include <cassert>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "./concepts.h"
#include "./image_state.h"
#include "./mock_vulkan.h"

/// domain、pending_value、tracked_value：默认实现，让 tracked_resource/pending_resource
/// 这两个 concept 落地成能真的跑起来的东西。全部放进 gpu::mock（不是裸的
/// gpu），跟 gpu::vk（vulkan_tracked_value.h）保持同一种结构——这不只是命名
/// 一致性：本文件下面的 make_pending 要靠 ADL 被 algorithms.h 里的 make_pending
/// CPO 找到，如果这些类型直接放在 gpu 里，跟 CPO 对象本身（gpu::make_pending）
/// 撞在同一个命名空间，会变成"重定义"错误，不是简单的遮蔽——CPO 的分派必须靠
/// ADL 在实参的关联命名空间里找到实现，实现和调用点对象不能同名同空间。
///
/// domain 沿 sender 链的传递方式：前置 schedule(dom) 表达域，走标准 stdexec 的
/// scheduler/completion-scheduler 机制，不走 env 侧信道，也不是自定义的窄查询。
/// 具体调查过 nvexec 的 stream_scheduler：它给自己配一个满足 stdexec::scheduler
/// 的类型，sender 的 attrs 应答标准查询 get_completion_scheduler_t<set_value_t>；
/// 下游算子在 connect 阶段用 get_completion_scheduler<set_value_t>(get_env(pred))
/// 查出这个 scheduler，从中取出真正的资源（CUDA stream / 这里的 domain）。
/// mock_scheduler 就是 gpu::mock 这一侧对应 nvexec stream_scheduler 的角色。
///
/// transition（algorithms.h 里的 sender 算法）就是这套机制的消费者：它在
/// connect 阶段查出 predecessor 的 completion scheduler，如果这个 scheduler
/// 类型提供 transition_impl 这个定制点（mock_scheduler 在下面提供），就调用它
/// （录 barrier）；没有提供就走默认行为（只调用 transite，不做任何 GPU 调用）。
/// 这跟 nvexec 用 transform_sender_for<Tag> 做域改写是同一件事的简化版——我们
/// 不借助 stdexec 内部依赖 tag_of_t/__sexpr 的 transform_sender 机制（那要求
/// sender 是标准算子风格的表达式节点），而是在 transition 自己的算子状态里手写
/// 同样的"查 scheduler、按需分派到定制实现"逻辑，效果一致。
namespace gpu::mock
{
class domain
{
public:
	[[nodiscard]] command_buffer_handle command_buffer() const noexcept { return _command; }

	void add_wait(signal_handle signal) { _waits.push_back(signal); }

	/// 结束当前命令缓冲、提交、拿到这批的信号，开一个新的命令缓冲接着录。
	[[nodiscard]] signal_handle cut()
	{
		auto const signal = queue_submit(_command, _waits);
		_waits.clear();
		_command = next_handle();
		return signal;
	}

private:
	command_buffer_handle _command = next_handle();
	::std::vector<signal_handle> _waits{};
};

template <class Handle, class State>
struct tracked_value;

template <class Handle, class State>
struct pending_value;

/// mock_scheduler：满足 stdexec::scheduler，是 domain 对外暴露给 sender 链的
/// 身份。algorithms.h::transition/consume 通过标准的
/// get_completion_scheduler<set_value_t> 查询拿到它，再调用下面的
/// transition_impl/consume_impl——这两个不是 cpo.h 里的原语，是这个具体域自己
/// 提供的定制点实现，跟 transite（纯代数原语）是两层不同的东西：transite 只管
/// "状态标签怎么变"，transition_impl 管"这次变换要不要在 domain 上录点什么"。
///
/// 定义在 mock_schedule_sender 之前：schedule() 返回的 sender 需要持有一份
/// 完整的 mock_scheduler（不是前向声明），顺序反过来会是不完整类型错误。
/// mock_scheduler 自己不需要 mock_schedule_sender 的完整定义（schedule()
/// 只是声明返回类型，函数体延后到 mock_schedule_sender 定义完再给出）。
class mock_scheduler;

struct mock_schedule_sender;

class mock_scheduler
{
public:
	explicit mock_scheduler(domain& dom) noexcept : _domain(&dom) {}

	[[nodiscard]] bool operator==(mock_scheduler const&) const noexcept = default;

	[[nodiscard]] mock_schedule_sender schedule() const noexcept;

	[[nodiscard]] domain& get_domain() const noexcept { return *_domain; }

	/// transition 的定制实现：先做代数变换（transite），再在 domain 上录 barrier。
	/// 按 From/To 是否同属 image_state 或 buffer_state 分支——只有这一处需要知道
	/// "这是不是一张图"，跟之前版本 transite_sender 里的分支逻辑一样，只是现在
	/// 挂在 domain 的定制点上，不挂在代数原语 transite 身上。
	template <class Handle, class From, class To>
	[[nodiscard]] tracked_value<Handle, To> transition_impl(tracked_value<Handle, From> const& value, To to) const
	{
		if constexpr (::gpu::image_state<From> && ::gpu::image_state<To>)
		{
			auto const from = value.state();
			cmd_pipeline_barrier(_domain->command_buffer(),
				from.stage(), from.access(), to.stage(), to.access(),
				from.layout(), to.layout(), value.resource());
		}
		else if constexpr (::gpu::buffer_state<From> && ::gpu::buffer_state<To>)
		{
			auto const from = value.state();
			cmd_buffer_barrier(_domain->command_buffer(),
				from.stage(), from.access(), to.stage(), to.access(), value.resource());
		}
		else
		{
			static_assert(::gpu::detail::always_false<To>, "transition: From/To 必须同属 image_state 或同属 buffer_state");
		}
		return ::gpu::transite(value, to);
	}

	/// consume 的定制实现：还在这个域的录制链里，把信号记成"这一批要等的东西"
	/// ——对应 CUDA 的 cudaStreamWaitEvent。跟 transition_impl 一样是域自己的
	/// 定制点，不是原语，接收整个 pending_value（跟 transition_impl(value, to)
	/// 接收整个 tracked_value 是同一种形状），由 mock_scheduler 自己读
	/// _handle/_state/_signal 这几个字段——pending_resource 这一侧没有像
	/// tracked_resource 那样拆出通用的字段读取原语（按设计讨论的结论，先搁置），
	/// 所以这个定制点只对 gpu::mock::pending_value 这个具体类型生效，
	/// algorithms.h::dispatch_consume 用 requires 检测这条路径是否可行，检测
	/// 不到就落回 consume_external 原语。返回 sender（不是裸值）：跟
	/// consume_external 原语的返回类型保持同一种形状，这样 dispatch_consume
	/// 两条分支产出的东西可以用同一套标准组合子去接，不需要手写一个类型擦除的
	/// 算子状态去兼容两种不同形状的返回值。
	template <class Handle, class State>
	[[nodiscard]] auto consume_impl(pending_value<Handle, State> const& value) const
	{
		_domain->add_wait(value._signal);
		return ::stdexec::just(tracked_value<Handle, State>{value._handle, value._state});
	}

private:
	domain* _domain;
};

/// schedule(dom) 产出的 sender。定义在 mock_scheduler 之后：需要持有一份完整的
/// mock_scheduler 作为字段（不是指针/引用），跟 nvexec 的 stream_scheduler::
/// sender 是同一种形状——sender 本身按值携带调度器，attrs 直接把它答出去。
struct mock_schedule_sender
{
	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = ::stdexec::completion_signatures<::stdexec::set_value_t()>;

	mock_scheduler _scheduler;

	struct attrs
	{
		mock_scheduler _scheduler;

		[[nodiscard]] mock_scheduler query(::stdexec::get_completion_scheduler_t<::stdexec::set_value_t>) const noexcept
		{
			return _scheduler;
		}
	};

	[[nodiscard]] attrs get_env() const noexcept { return {_scheduler}; }

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;
		Receiver _receiver;
		void start() & noexcept { ::stdexec::set_value(::std::move(_receiver)); }
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const
	{
		return {::std::move(receiver)};
	}
};

[[nodiscard]] inline mock_schedule_sender mock_scheduler::schedule() const noexcept
{
	return {*this};
}
static_assert(::stdexec::scheduler<mock_scheduler>);

[[nodiscard]] inline mock_schedule_sender schedule(domain& dom) noexcept
{
	return mock_scheduler{dom}.schedule();
}

/// submit 产出的通用 pending 类型：句柄 + 状态 + 这批提交出来的信号。对任何满足
/// tracked_resource 的 T 都适用（见 algorithms.h 的 submit），不需要再多一个 CPO
/// 去问"你的 pending 形态长什么样"——直接复用 resource/state 这两个已有原语构造。
template <class Handle, class State>
struct pending_value
{
	Handle _handle;
	State _state;
	signal_handle _signal;

	/// 没有域在录制链里时的默认消费方式：真的等这个信号——对应
	/// cudaEventSynchronize。这是 consume_external 原语自己的默认实现（不涉及
	/// 任何具体 domain），algorithms.h::consume 在查到域时会改用
	/// mock_scheduler::consume_impl，查不到域才落到这里。
	[[nodiscard]] auto consume_external() const
	{
		return ::stdexec::just() | ::stdexec::then([signal = _signal, handle = _handle, state = _state]
		{
			wait_signal(signal);
			return tracked_value<Handle, State>{handle, state};
		});
	}
};

/// make_pending 的 ADL 实现：algorithms.h::submit 靠这个把"提交完的 tracked_resource
/// + 这批的信号"包成本命名空间的 pending_value，不需要 submit 认识这个类型模板。
template <class Handle, class State>
[[nodiscard]] pending_value<Handle, State> make_pending(tracked_value<Handle, State> const& item, signal_handle signal)
{
	return {::gpu::resource(item), ::gpu::state(item), signal};
}

/// tracked_resource / transitible_tracked_resource 的默认实现：resource()/
/// state() 是纯字段读取；transite(to) 是纯代数变换——只换状态标签，不做任何
/// GPU 调用、不知道 domain 存在。真正"要不要录 barrier"是 mock_scheduler::
/// transition_impl（域的定制点）的事，两者是完全独立的两层。
template <class Handle, class State>
struct tracked_value
{
	Handle _handle;
	State _state;

	[[nodiscard]] Handle resource() const noexcept { return _handle; }
	[[nodiscard]] State state() const noexcept { return _state; }

	template <class To>
	[[nodiscard]] tracked_value<Handle, To> transite(To to) const
	{
		return {_handle, ::std::move(to)};
	}
};

static_assert(::gpu::transitible_tracked_resource<tracked_value<image_handle, ::gpu::undefined>>);
static_assert(::gpu::pending_resource<pending_value<image_handle, ::gpu::undefined>>);
}
