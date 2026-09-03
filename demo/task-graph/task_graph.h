#pragma once

/// 每帧重建的任务图（task graph）——**只有原语**。
///
/// 图就是 sender 表达式：扇出用 `::exec::split`、扇入用 `::stdexec::when_all`、
/// 先后由表达式嵌套表达。本文件不做拓扑排序、不管前驱计数，只补 stdexec 缺的东西：
///
/// | 原语 | 干什么 |
/// | --- | --- |
/// | `dynamic_when_all` | 元素个数运行期才知道的扇入。`when_all` 是变参包，凑不出来 |
/// | `node_sender` | 一条边的通用形态（类型擦除）。动态扇入要求孩子同构 |
/// | `frame_context` | 一帧的上下文：托管 job、按 owner 查 job、收根节点、带取消源 |
/// | `run_frame` | 拿上下文启动整张图，跑完返回 |
///
/// 扇出直接用 `::exec::split`，取消直接用 `::stdexec::write_env` + `stop_token()`，
/// 汇合点直接用 `::std::vector<node_sender>`——这三样都不包一层。
///
/// **协议**（三步）：
///
///  1. 阶段 A —— `entity.frame_job(context)`：entity 自己调
///     `context.add_job<job_type>(this, 构造参数...)`。**job 归上下文托管**，
///     entity 不持有它——于是一个 entity 可以挂好几个 job，帧一结束全部逆序销毁，
///     不存在"上一帧那个还躺着"。job 的构造函数里**不许访问别的 job**。
///  2. 阶段 B —— `job.build()`：把若干 sender 挂进 `context`（或别的汇合点）。
///     依赖别的 entity 就用 `context.job_for<其 job 类型>(&那个 entity)` 去取——
///     `run_frame` 保证阶段 A 全做完才进阶段 B，所以**注册顺序无关**。
///  3. `run_frame(context)` —— 取走根节点、连接、启动、等完成。
///
/// **取消不走结构边。** `::exec::split` 在订阅者令牌已停止时直接 `set_stopped`、根本
/// 不启动共享体，所以取消若沿结构边下压，收尾节点（等 fence 之类）就一次都不会跑。
/// `run_frame` 给根接收者的是一个永不停止的令牌；想响应取消的节点自己写
/// `write_env(表达式, prop{get_stop_token, context.stop_token()})` 把令牌注进去。
///
/// **不管的事**：调度器与帧数据（从 `frame_context` 派生自己加）、entity 名单（是
/// 调用者的）、汇合点要不要防"迟到注册"（是使用者的，库只给裸容器）。

#include <atomic>
#include <cassert>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <utility>
#include <vector>

#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

namespace bvn::task_graph
{

// ------------------------------------------------------------------ 原地构造

/// 存放不可移动对象的手工生命周期槽。
///
/// op-state 是地址敏感的（子接收者持有指向它的指针），既不可拷贝也不可移动，于是
/// `::std::optional::emplace` 这条路走不通——它是直接初始化，会去找移动构造。
/// 这里走 `::new (地址) T(工厂())`：工厂返回 T 的纯右值，C++17 保证省略，
/// 对象直接落在槽里。
template <class value_type>
struct manual_lifetime
{
	alignas(value_type) ::std::byte _storage[sizeof(value_type)];
	bool _engaged = false;

	manual_lifetime() = default;
	manual_lifetime(manual_lifetime const&) = delete;
	auto operator=(manual_lifetime const&) -> manual_lifetime& = delete;

	~manual_lifetime()
	{
		reset();
	}

	/// `factory` 必须返回 `value_type` 的纯右值——靠保证省略直接在槽里构造。
	template <class factory_type>
	auto construct(factory_type&& factory) -> value_type&
	{
		assert(!_engaged);
		auto const object = ::new (static_cast<void*>(_storage)) value_type(::std::forward<factory_type>(factory)());
		_engaged = true;
		return *object;
	}

	auto reset() noexcept -> void
	{
		if (_engaged)
		{
			_engaged = false;
			::std::launder(reinterpret_cast<value_type*>(_storage))->~value_type();
		}
	}

	[[nodiscard]] auto get() noexcept -> value_type&
	{
		assert(_engaged);
		return *::std::launder(reinterpret_cast<value_type*>(_storage));
	}
};

// ------------------------------------------------------------------- 一条边

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
template <class sender_type>
struct env_gate_sender
{
	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = node_completions;

	template <class receiver_type>
	struct gate_receiver
	{
		using receiver_concept = ::stdexec::receiver_t;

		receiver_type* _receiver;

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

	template <class receiver_type>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		// 先声明再连接：`gate_receiver` 攥的是 `_receiver` 的地址，成员按声明序初始化。
		receiver_type _receiver;
		::stdexec::connect_result_t<sender_type, gate_receiver<receiver_type>> _inner;

		operation(sender_type sender, receiver_type receiver)
			: _receiver(::std::move(receiver))
			, _inner(::stdexec::connect(::std::move(sender), gate_receiver<receiver_type>{&_receiver}))
		{
		}

		operation(operation const&) = delete;
		auto operator=(operation const&) -> operation& = delete;

		auto start() & noexcept -> void
		{
			::stdexec::start(_inner);
		}
	};

	sender_type _sender;

	template <::stdexec::receiver receiver_type>
	[[nodiscard]] auto connect(receiver_type receiver) && -> operation<receiver_type>
	{
		return operation<receiver_type>{::std::move(_sender), ::std::move(receiver)};
	}
};

/// 把任意"不传值、错误是 `::std::exception_ptr`"的 sender 装成帧图的一条边。
///
/// 共享节点的正确用法是先 `::exec::split`，再把 split sender 的**拷贝**喂给这里：
/// split sender 可拷贝，`node_sender` 只可移动，所以每个消费者拿一份新的擦除盒，
/// 共享的是盒子里那个节点。
template <class sender_type>
[[nodiscard]] auto make_node(sender_type sender) -> node_sender
{
	return node_sender{env_gate_sender<sender_type>{::std::move(sender)}};
}

// ---------------------------------------------------------------- 动态扇入

/// 元素个数到启动期才知道的 `when_all`。
///
/// 为什么不是"逐个 spawn 进 async_scope"：spawn 丢错误、丢取消，而 `scope.on_empty()`
/// 是"空了"不是"汇合"。这里是一个真正的汇合子：一个倒计数器 + 首个非正常完成胜出。
///
/// **跟 `::stdexec::when_all` 的唯一语义差别：孩子出错 / 被取消时不广播取消给兄弟。**
/// 标准 `when_all` 会广播，但那跟"取消不走结构边"直接冲突：兄弟里但凡有个收尾节点
/// （`split` 出来的清理边），被广播停掉就在订阅侧短路，收尾体一次都不跑。而且谁先
/// 完成是竞态，于是漏不漏收尾也成了竞态。这里选 **correctness over fail-fast**：
/// 所有孩子都跑完，第一个非正常完成的结果胜出。想 fail-fast 的话在自己的节点里做。
///
/// 外层要求取消时仍然照转（`_on_stop`）——那是 sender 该有的行为，跟这条无关。
///
/// 孩子同构且不传值，所以完成签名写死，不需要 `transform_completion_signatures`，
/// 也不需要折叠值 variant。
template <class child_sender>
struct dynamic_when_all_sender
{
	static_assert(::stdexec::sender_in<child_sender, node_env>);

	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = node_completions;

	enum class completion_kind
	{
		value,
		error,
		stopped,
	};

	template <class receiver_type>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		struct child_receiver
		{
			using receiver_concept = ::stdexec::receiver_t;

			operation* _operation;

			auto set_value() noexcept -> void
			{
				_operation->arrive();
			}

			auto set_error(::std::exception_ptr error) noexcept -> void
			{
				_operation->fail(::std::move(error));
				_operation->arrive();
			}

			auto set_stopped() noexcept -> void
			{
				_operation->stopped();
				_operation->arrive();
			}

			[[nodiscard]] auto get_env() const noexcept -> node_env
			{
				return node_env{._stop_token = _operation->_stop_source.get_token()};
			}
		};

		struct forward_stop
		{
			operation* _operation;

			auto operator()() const noexcept -> void
			{
				_operation->_stop_source.request_stop();
			}
		};

		using child_operation_type = ::stdexec::connect_result_t<child_sender, child_receiver>;
		using outer_token_type = ::stdexec::stop_token_of_t<::stdexec::env_of_t<receiver_type>>;
		using stop_callback_type = typename outer_token_type::template callback_type<forward_stop>;

		receiver_type _receiver;
		::std::vector<child_sender> _sources;
		::std::size_t _count = 0;
		::std::unique_ptr<manual_lifetime<child_operation_type>[]> _operations;
		::std::atomic<::std::size_t> _pending{0};
		::std::atomic<completion_kind> _kind{completion_kind::value};
		::std::exception_ptr _error;
		::stdexec::inplace_stop_source _stop_source;
		::std::optional<stop_callback_type> _on_stop;

		operation(::std::vector<child_sender> sources, receiver_type receiver)
			: _receiver(::std::move(receiver))
			, _sources(::std::move(sources))
			, _count(_sources.size())
			, _operations(::std::make_unique<manual_lifetime<child_operation_type>[]>(_count))
		{}

		operation(operation const&) = delete;
		auto operator=(operation const&) -> operation& = delete;

		auto start() & noexcept -> void
		{
			if (_count == 0)
			{
				::stdexec::set_value(::std::move(_receiver));
				return;
			}

			// +1 是留给下面这个启动循环自己的：不然某个同步完成的孩子会在循环还没走完时
			// 把计数减到 0，触发完成、进而销毁 *this，后面的迭代就踩在死对象上。
			_pending.store(_count + 1, ::std::memory_order_relaxed);
			_on_stop.emplace(::stdexec::get_stop_token(::stdexec::get_env(_receiver)), forward_stop{this});

			try
			{
				// 先全部 connect，再全部 start。反过来的话，第一个孩子同步完成时
				// 后面的孩子还没连上，它们的 op-state 就永远不会存在。
				for (auto index = ::std::size_t{0}; index != _count; ++index)
				{
					_operations[index].construct([&]
					{
						return ::stdexec::connect(::std::move(_sources[index]), child_receiver{this});
					});
				}
			}
			catch (...)
			{
				// 还没 start 的 op-state 直接销毁即可——没启动就没有完成的义务。
				for (auto index = ::std::size_t{0}; index != _count; ++index)
				{
					_operations[index].reset();
				}

				_on_stop.reset();
				::stdexec::set_error(::std::move(_receiver), ::std::current_exception());
				return;
			}

			_sources.clear();

			for (auto index = ::std::size_t{0}; index != _count; ++index)
			{
				::stdexec::start(_operations[index].get());
			}

			arrive();
		}

		/// 首个非正常完成胜出。**不广播取消**——见类型注释。
		auto fail(::std::exception_ptr error) noexcept -> void
		{
			auto expected = completion_kind::value;
			if (_kind.compare_exchange_strong(expected, completion_kind::error, ::std::memory_order_acq_rel))
			{
				_error = ::std::move(error);
			}
		}

		auto stopped() noexcept -> void
		{
			auto expected = completion_kind::value;
			static_cast<void>(_kind.compare_exchange_strong(
				expected, completion_kind::stopped, ::std::memory_order_acq_rel));
		}

		auto arrive() noexcept -> void
		{
			if (_pending.fetch_sub(1, ::std::memory_order_acq_rel) != 1)
			{
				return;
			}

			_on_stop.reset();

			switch (_kind.load(::std::memory_order_acquire))
			{
			case completion_kind::value:
				::stdexec::set_value(::std::move(_receiver));
				return;
			case completion_kind::error:
				::stdexec::set_error(::std::move(_receiver), ::std::move(_error));
				return;
			case completion_kind::stopped:
				::stdexec::set_stopped(::std::move(_receiver));
				return;
			}
		}
	};

	::std::vector<child_sender> _children;

	// 单发射：孩子只可移动，连接即消费。`let_value` 以右值连接它返回的 sender
	// （__let.hpp 里 `__nothrow_connectable<__sndr2_t, __rcvr2_t>` 用的是非引用类型），
	// `run_frame` 也是就地构造后立刻连，两处都够用。
	template <::stdexec::receiver receiver_type>
	[[nodiscard]] auto connect(receiver_type receiver) && -> operation<receiver_type>
	{
		return operation<receiver_type>{::std::move(_children), ::std::move(receiver)};
	}
};

template <class child_sender>
[[nodiscard]] auto dynamic_when_all(::std::vector<child_sender> children) -> dynamic_when_all_sender<child_sender>
{
	return dynamic_when_all_sender<child_sender>{._children = ::std::move(children)};
}

// -------------------------------------------------------------- 一帧的上下文

/// 一个 job 只需要能"把自己挂进本帧"。
///
/// 注意它**不返回**节点：交出根节点是"外围来拿"，挂进去才是"job 自己放"。
template <class job_type>
concept graph_job = requires (job_type& job)
{
	{ job.build() } -> ::std::same_as<void>;
};

/// 每个类型一个唯一地址。给托管的 job 打类型标签用，不需要 RTTI。
template <class type>
inline constexpr char job_type_tag = 0;

/// 一帧的上下文：托管 job、按 owner 查 job、收根节点、带本帧的取消源。
///
/// 调度器、帧号、时间步这些**不在这里**——它们是使用者的帧数据，从本类型派生一个
/// 自己的上下文加上去即可，`run_frame` 只认基类这部分。
struct frame_context
{
	struct job_entry
	{
		void const* _owner = nullptr;
		void const* _tag = nullptr;
		void* _job = nullptr;
		void (*_build)(void*) = nullptr;
		void (*_destroy)(void*) = nullptr;
	};

	/// 本帧全部 job，**归上下文所有**。entity 不持有 job，于是一个 entity 可以挂
	/// 好几个，而且帧一结束就全没了——不存在"上一帧那个还躺着"要去比对帧号。
	::std::vector<job_entry> _jobs;

	/// 阶段 B 收下的根节点。就是个裸容器，不封不锁——要不要防"挂晚了"是使用者的事。
	::std::vector<node_sender> _roots;

	/// 本帧的取消源。外部令牌一停就转发到这里，节点自己用 `write_env` 把它注进去。
	::stdexec::inplace_stop_source _stop_source;

	/// 结构边用的令牌源，**从不 `request_stop`**。图的骨架不参与取消，否则
	/// `::exec::split` 会在订阅侧短路，收尾节点就一次都不跑。
	::stdexec::inplace_stop_source _structural_source;

	frame_context() = default;
	frame_context(frame_context const&) = delete;
	auto operator=(frame_context const&) -> frame_context& = delete;

	~frame_context()
	{
		// 逆序销毁：后造的可能引用先造的。
		for (auto index = _jobs.size(); index != 0; --index)
		{
			auto&& entry = _jobs[index - 1];
			entry._destroy(entry._job);
		}
	}

	/// 阶段 A：entity 造好本帧 job 并交给上下文托管。
	///
	/// `owner` 是查找用的 key，传 entity 自己的地址即可——依赖方手里攥着的正是那个
	/// entity 的引用，于是依赖关系仍然是具体类型直连，不退化成字符串 / tag 查表。
	template <graph_job job_type, class... argument_types>
	auto add_job(void const* owner, argument_types&&... arguments) -> job_type&
	{
		auto owned = ::std::make_unique<job_type>(::std::forward<argument_types>(arguments)...);

		_jobs.push_back(job_entry{
			._owner = owner,
			._tag = &job_type_tag<job_type>,
			._job = owned.get(),
			._build = [](void* job) { static_cast<job_type*>(job)->build(); },
			._destroy = [](void* job) { delete static_cast<job_type*>(job); },
		});

		return *owned.release();
	}

	/// 弱依赖：对端本帧可能不在场，由调用方决定怎么办。线性扫描，不分配。
	template <class job_type>
	[[nodiscard]] auto find_job(void const* owner) const noexcept -> job_type*
	{
		for (auto&& entry : _jobs)
		{
			if (entry._owner == owner && entry._tag == &job_type_tag<job_type>)
			{
				return static_cast<job_type*>(entry._job);
			}
		}

		return nullptr;
	}

	/// 强依赖：对端本帧不在场就是非法状态，当场点名。
	template <class job_type>
	[[nodiscard]] auto job_for(void const* owner) const -> job_type&
	{
		auto const job = find_job<job_type>(owner);
		if (job == nullptr)
		{
			throw ::std::logic_error{"task graph: 依赖的 entity 未参与本帧"};
		}

		return *job;
	}

	/// 阶段 B：把一条根节点挂进本帧。挂几条、挂不挂，由 job 自己决定。
	auto add(node_sender node) -> void
	{
		_roots.push_back(::std::move(node));
	}

	/// 想响应取消的节点自己写：
	/// `write_env(表达式, prop{get_stop_token, context.stop_token()})`。
	[[nodiscard]] auto stop_token() const noexcept -> ::stdexec::inplace_stop_token
	{
		return _stop_source.get_token();
	}
};

// ------------------------------------------------------------------ 跑一帧

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
	for (auto&& entry : context._jobs)
	{
		entry._build(entry._job);
	}

	auto state = run_state{._error = {}, ._stop_token = context._structural_source.get_token()};
	auto operation = ::stdexec::connect(
		dynamic_when_all(::std::move(context._roots)), run_receiver{&state});
	::stdexec::start(operation);
	state._done.acquire();

	if (state._error)
	{
		::std::rethrow_exception(state._error);
	}

	return !state._stopped;
}

}
