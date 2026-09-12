#pragma once

#include <exception>
#include <utility>

#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

namespace bvn::task_graph
{

/// 帧图里每条边的完成形状：不传值、错误统一 `::std::exception_ptr`、可取消。
using node_completions = ::stdexec::completion_signatures<
	::stdexec::set_value_t(),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>;

/// 节点看到的环境。只留停止令牌——帧图的节点不传值，也不需要 domain / allocator。
struct node_env
{
	::stdexec::inplace_stop_token _stop_token;

	[[nodiscard]] constexpr auto query(::stdexec::get_stop_token_t) const noexcept -> ::stdexec::inplace_stop_token
	{
		return _stop_token;
	}
};

/// 擦除后的接收者。第二个模板参数是**要穿过擦除边界的环境查询清单**。
///
/// 声明了 `get_stop_token`，`::exec::any_sender` 的 op-state 就会按外层接收者的令牌
/// 类型自动选路——可转换成 `inplace_stop_token` 时零开销直通，否则自带一个
/// `inplace_stop_source` 把外层取消转发进来（见 any_sender_of.hpp 的
/// `_state<Receiver, inplace_stop_token>`）。
using node_receiver = ::exec::any_receiver<
	node_completions,
	::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept>>;

/// 帧图里一条边的通用形态。只可移动：连接即消费，一条边本帧只跑一次。
using node_sender = ::exec::any_sender<node_receiver>;

static_assert(::stdexec::sender<node_sender>);

/// 把环境钉死在 `node_env` 上的适配器——**规避 stdexec 的一个上游 bug**。
///
/// `node_receiver` 的环境是那个多态接口本身（`any_sender_of.hpp` 里
/// `_interface_::get_env() -> _interface_ const&`），它继承自 `__any::__interface_base`，
/// 拷贝构造是 deleted，因此既不可拷贝也不可移动。而 `__continues_on.hpp:228` 拿
/// `__fwd_env_t<_Env>`（按**值**）去转发环境，落到 `__env::__fwd<_Env>` 里的
/// `static_assert(__nothrow_move_constructible<_Env>)` 上直接炸。于是"带查询的
/// `any_sender` + 任何含 `starts_on` / `continues_on` 的表达式"编译不过。
/// 本地 pin 的 f91f6363 和上游 HEAD 4754c76d 都还带着它。
///
/// 这一层让被擦除的表达式看到的外层环境固定是 `node_env`（可平凡移动），多态接口
/// 就再也进不到 `__fwd_env_t` 里去了。停止令牌照旧穿过去。完成签名写死，
/// `any_sender` 也就不必再拿那个环境去递归推导孩子的签名。
template <class Sender>
struct env_gate_sender
{
	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = node_completions;

	template <class Receiver>
	struct gate_receiver
	{
		using receiver_concept = ::stdexec::receiver_t;

		Receiver* _receiver;

		auto set_value() noexcept -> void
		{
			::stdexec::set_value(::std::move(*_receiver));
		}

		auto set_error(::std::exception_ptr error) noexcept -> void
		{
			::stdexec::set_error(::std::move(*_receiver), ::std::move(error));
		}

		auto set_stopped() noexcept -> void
		{
			::stdexec::set_stopped(::std::move(*_receiver));
		}

		[[nodiscard]] auto get_env() const noexcept -> node_env
		{
			return node_env{._stop_token = ::stdexec::get_stop_token(::stdexec::get_env(*_receiver))};
		}
	};

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		// 先声明再连接：`gate_receiver` 攥的是 `_receiver` 的地址，成员按声明序初始化。
		Receiver _receiver;
		::stdexec::connect_result_t<Sender, gate_receiver<Receiver>> _inner;

		operation(Sender sender, Receiver receiver)
			: _receiver(::std::move(receiver))
			, _inner(::stdexec::connect(::std::move(sender), gate_receiver<Receiver>{&_receiver}))
		{
		}

		operation(operation const&) = delete;
		auto operator=(operation const&) -> operation& = delete;

		auto start() & noexcept -> void
		{
			::stdexec::start(_inner);
		}
	};

	Sender _sender;

	template <::stdexec::receiver Receiver>
	[[nodiscard]] auto connect(Receiver receiver) && -> operation<Receiver>
	{
		return operation<Receiver>{::std::move(_sender), ::std::move(receiver)};
	}
};

/// 把任意"不传值、错误是 `::std::exception_ptr`"的 sender 装成帧图的一条边。
///
/// 共享节点的正确用法是先 `::exec::split`，再把 split sender 的**拷贝**喂给这里：
/// split sender 可拷贝，`node_sender` 只可移动，所以每个消费者拿一份新的擦除盒，
/// 共享的是盒子里那个节点。
template <class Sender>
[[nodiscard]] auto make_node(Sender sender) -> node_sender
{
	return node_sender{env_gate_sender<Sender>{::std::move(sender)}};
}

}
