#pragma once

#include <cassert>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "./algorithms.h"
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
/// domain 沿 sender 链的传递、以及"域怎么接管标准算子"这两件事，都改成用 stdexec
/// 真正的公开机制，不是我们自己模拟的简化版：
///
/// 1. scheduler 暴露自己：mock_scheduler 满足 stdexec::scheduler，
///    mock_schedule_sender 的 attrs 应答标准查询
///    get_completion_scheduler_t<set_value_t>，跟 nvexec::stream_scheduler
///    完全一样的做法。
/// 2. scheduler 暴露自己的 completion domain：mock_scheduler 额外应答
///    get_completion_domain_t<set_value_t>，返回 mock_domain{}——这一步是
///    nvexec::stream_scheduler 真实在做的事（stream_context.cuh 里
///    attrs::query(get_completion_domain_t<set_value_t>) 返回 stream_domain{}），
///    不是我们发明的。
/// 3. 域接管标准算子：mock_domain 提供 transform_sender(set_value_t, sndr, env)
///    成员，用 stdexec::tag_of_t<Sender> 认出这是 gpu::transition_t 还是
///    gpu::consume_t，命中就换成下面的域专属 sender（调用
///    mock_scheduler::transition_impl/consume_impl）；stdexec::connect() 会在
///    真正 connect 之前自动调用 stdexec::transform_sender(sndr, get_env(rcvr))，
///    这一步本身就会顺着 attrs 转发链查出 predecessor 的 completion domain 并
///    调用这个函数——mock_domain 不需要被谁显式调用。
///
/// algorithms.h 里的 transition_sender/consume_sender 是"没有域接管时"的默认
/// 聚合体，本身完全不知道 mock_domain 存在，也不做任何查询——分派已经在
/// connect() 里被 transform_sender 处理掉了，两层职责严格分开。
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

/// mock_domain：mock_scheduler 的 completion domain，针对 gpu::transition_t/
/// gpu::consume_t 提供 transform_sender——真正接管这两个算子的地方。前向声明在
/// mock_scheduler 之前：mock_scheduler::query(get_completion_domain_t<...>) 要
/// 把它当返回类型，函数体在类内给出（返回类型只是声明为 mock_domain，不要求
/// 这里已经是完整类型，是函数体本身要构造它才需要），mock_domain 的完整定义
/// 延后到 tracked_value/pending_value 都完整之后再给出。
class mock_domain;

/// mock_scheduler：满足 stdexec::scheduler，是 domain 对外暴露给 sender 链的
/// 身份。它同时应答两个标准查询：get_completion_scheduler_t<set_value_t>（把
/// 自己交出去）和 get_completion_domain_t<set_value_t>（把 mock_domain 交
/// 出去）——跟 nvexec::stream_scheduler 完全一样的两步暴露方式（调查过
/// stream_context.cuh 的 attrs::query 两个重载）。transition_impl/consume_impl
/// 是这个具体域自己提供的定制点实现，不是 cpo.h 里的原语，也不是 sender 算法本身
/// 直接调用它们——调用它们的是 mock_domain::transform_sender 换出来的专属
/// sender，algorithms.h 里的默认聚合体完全不知道这两个函数存在。
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

	/// 跟 mock_schedule_sender::attrs 里那份是同一个查询，重复应答一次：
	/// stdexec::get_completion_domain_t 内部有个一致性检查
	/// （__check_domain_，__domain.hpp），如果只能从 attrs 查到 completion
	/// domain、从 scheduler 本身查不到，会认为这是矛盾的状态而 static_assert
	/// 失败——两处都要答，跟 nvexec::stream_scheduler 用 CRTP 基类
	/// stream_scheduler_env 同时给 scheduler 和它的 sender 提供同一份查询是
	/// 同一个原因。
	[[nodiscard]] mock_domain query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>) const noexcept;

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
	/// 所以这个定制点只对 gpu::mock::pending_value 这个具体类型生效。返回裸值
	/// （不是 sender）：跟 transition_impl 保持同一种形状，调用方
	/// （mock_domain::transform_sender 换出来的专属 sender）自己决定怎么包成
	/// sender 的完成值。
	template <class Handle, class State>
	[[nodiscard]] tracked_value<Handle, State> consume_impl(pending_value<Handle, State> const& value) const
	{
		_domain->add_wait(value._signal);
		return {value._handle, value._state};
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

		// mock_domain 还不完整（完整定义在文件靠后，需要 tracked_value/
		// pending_value 都完整），函数体延后到 mock_domain 定义完再给出，跟
		// mock_scheduler::schedule() 用的是同一个手法。
		[[nodiscard]] mock_domain query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>) const noexcept;
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

/// mock_domain::transform_sender 换出来的 transition 专属实现：调用
/// mock_scheduler::transition_impl（会录 barrier），而不是默认聚合体
/// 里的 ::gpu::transite（纯代数变换）。跟 algorithms.h::transition_sender
/// 是同一种形状（聚合体、手写 operation、Pred 存成 operation 自己的成员避免
/// 悬空引用），区别只在 set_value 里调哪个函数。
template <class To, class Pred>
struct domain_transition_sender
{
	To _to;
	Pred _pred;
	mock_scheduler _scheduler;

	using sender_concept = ::stdexec::sender_t;

	using __value_t = ::stdexec::value_types_of_t<Pred, ::stdexec::env<>, ::gpu::detail::single_value_t, ::gpu::detail::single_value_t>;
	using __result_t = decltype(_scheduler.transition_impl(::std::declval<__value_t const&>(), ::std::declval<To>()));
	using completion_signatures = ::stdexec::completion_signatures<
		::stdexec::set_value_t(__result_t),
		::stdexec::set_error_t(::std::exception_ptr),
		::stdexec::set_stopped_t()>;

	[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_pred); }

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		struct inner_receiver
		{
			using receiver_concept = ::stdexec::receiver_t;
			operation* _op;

			template <::gpu::transitible_tracked_resource Value>
			void set_value(Value&& value) noexcept
			{
				::stdexec::set_value(::std::move(_op->_receiver), _op->_sndr._scheduler.transition_impl(value, ::std::move(_op->_sndr._to)));
			}

			template <class Error>
			void set_error(Error&& error) noexcept { ::stdexec::set_error(::std::move(_op->_receiver), ::std::forward<Error>(error)); }

			void set_stopped() noexcept { ::stdexec::set_stopped(::std::move(_op->_receiver)); }

			[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_op->_receiver); }
		};

		domain_transition_sender _sndr;
		Receiver _receiver;
		::stdexec::connect_result_t<Pred&, inner_receiver> _inner;

		operation(domain_transition_sender sndr, Receiver receiver)
			: _sndr(::std::move(sndr)), _receiver(::std::move(receiver))
			, _inner(::stdexec::connect(_sndr._pred, inner_receiver{this}))
		{
		}

		void start() & noexcept { ::stdexec::start(_inner); }
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const &
	{
		return operation<Receiver>{*this, ::std::move(receiver)};
	}
};

/// mock_domain::transform_sender 换出来的 consume 专属实现：调用
/// mock_scheduler::consume_impl（记一条 wait），而不是默认聚合体里的
/// ::gpu::consume_external（真正等待）。
template <class Pred>
struct domain_consume_sender
{
	Pred _pred;
	mock_scheduler _scheduler;

	using sender_concept = ::stdexec::sender_t;

	using __value_t = ::stdexec::value_types_of_t<Pred, ::stdexec::env<>, ::gpu::detail::single_value_t, ::gpu::detail::single_value_t>;
	using __result_t = decltype(_scheduler.consume_impl(::std::declval<__value_t const&>()));
	using completion_signatures = ::stdexec::completion_signatures<
		::stdexec::set_value_t(__result_t),
		::stdexec::set_error_t(::std::exception_ptr),
		::stdexec::set_stopped_t()>;

	[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_pred); }

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		struct inner_receiver
		{
			using receiver_concept = ::stdexec::receiver_t;
			operation* _op;

			template <::gpu::pending_resource Value>
			void set_value(Value&& value) noexcept
			{
				::stdexec::set_value(::std::move(_op->_receiver), _op->_sndr._scheduler.consume_impl(value));
			}

			template <class Error>
			void set_error(Error&& error) noexcept { ::stdexec::set_error(::std::move(_op->_receiver), ::std::forward<Error>(error)); }

			void set_stopped() noexcept { ::stdexec::set_stopped(::std::move(_op->_receiver)); }

			[[nodiscard]] decltype(auto) get_env() const noexcept { return ::stdexec::get_env(_op->_receiver); }
		};

		domain_consume_sender _sndr;
		Receiver _receiver;
		::stdexec::connect_result_t<Pred&, inner_receiver> _inner;

		operation(domain_consume_sender sndr, Receiver receiver)
			: _sndr(::std::move(sndr)), _receiver(::std::move(receiver))
			, _inner(::stdexec::connect(_sndr._pred, inner_receiver{this}))
		{
		}

		void start() & noexcept { ::stdexec::start(_inner); }
	};

	template <class Receiver>
	[[nodiscard]] operation<Receiver> connect(Receiver receiver) const &
	{
		return operation<Receiver>{*this, ::std::move(receiver)};
	}
};

/// mock_domain：mock_scheduler 的 completion domain。跟 nvexec::stream_domain
/// 是同一个角色——针对特定算子 tag 提供 transform_sender，stdexec::connect() 会
/// 在真正 connect 之前自动调用 stdexec::transform_sender(sndr, get_env(rcvr))，
/// 这一步顺着 attrs 转发链查出 predecessor 的 completion domain（也就是这个
/// 类型），如果它对当前算子的 tag 提供 transform_sender 就调用，命中就把整条
/// 链在这一节的 sender 换成域专属实现——是 connect 之前的一次性结构替换，不是
/// 运行时判断。
///
/// 无状态、可默认构造：stdexec::get_completion_domain_t 内部有条路径需要能
/// value-initialize 查到的这个域类型本身（__read_query_t::operator()，
/// __domain.hpp），跟 nvexec::stream_domain 是空结构体一样，mock_domain 不带
/// 任何字段——需要哪个具体的 mock_scheduler，在 transform_sender 内部用标准查询
/// stdexec::get_completion_scheduler<set_value_t>(get_env(pred)) 从 predecessor
/// sender 身上现查，不提前存进 mock_domain。
///
/// 用 stdexec::tag_of_t<Sender> 认出 gpu::transition_t/gpu::consume_t：
/// transition_sender/consume_sender（algorithms.h）都是普通聚合体，第一个
/// 公开成员是 tag 对象，tag_of_t 靠结构化绑定识别这个形状，不需要
/// __sexpr/__make_sexpr 这类 stdexec 内部实现细节——跟 nvexec::stream_domain
/// 用 tag_of_t<Sender> 分派到 transform_sender_for<Tag> 是同一件事，只是这里
/// 没有再多一层 per-tag 特化模板，直接在 transform_sender 里用 if constexpr
/// 分两支。
class mock_domain
{
public:
	template <class Sender, class Env>
		requires ::std::same_as<::stdexec::tag_of_t<Sender>, ::gpu::transition_t>
	[[nodiscard]] auto transform_sender(::stdexec::set_value_t, Sender&& sndr, Env const&) const
	{
		auto&& [tag, to, pred] = ::std::forward<Sender>(sndr);
		auto scheduler = ::stdexec::get_completion_scheduler<::stdexec::set_value_t>(::stdexec::get_env(pred));
		return domain_transition_sender<::std::decay_t<decltype(to)>, ::std::decay_t<decltype(pred)>>{
			::std::forward<decltype(to)>(to), ::std::forward<decltype(pred)>(pred), scheduler};
	}

	template <class Sender, class Env>
		requires ::std::same_as<::stdexec::tag_of_t<Sender>, ::gpu::consume_t>
	[[nodiscard]] auto transform_sender(::stdexec::set_value_t, Sender&& sndr, Env const&) const
	{
		auto&& [tag, pred] = ::std::forward<Sender>(sndr);
		auto scheduler = ::stdexec::get_completion_scheduler<::stdexec::set_value_t>(::stdexec::get_env(pred));
		return domain_consume_sender<::std::decay_t<decltype(pred)>>{::std::forward<decltype(pred)>(pred), scheduler};
	}
};

[[nodiscard]] inline mock_domain mock_scheduler::query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>) const noexcept
{
	return {};
}

[[nodiscard]] inline mock_domain mock_schedule_sender::attrs::query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>) const noexcept
{
	return _scheduler.query(::stdexec::get_completion_domain_t<::stdexec::set_value_t>{});
}
}
