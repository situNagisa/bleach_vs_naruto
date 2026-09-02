#pragma once

/// 每帧重建的任务图（frame graph）执行框架。
///
/// 设计要点（对应设计讨论里的结论，详见同目录 README.md）：
///
///  1. **图就是 sender 表达式**。这一层不自己做拓扑排序、不自己管前驱计数：
///     扇出用 `split`、扇入用 `when_all` / `when_all_range`、先后由表达式嵌套表达。
///     本文件只补 stdexec 缺的三件小东西：具名共享节点的备忘格（`node_ref`）、
///     启动期才读取的动态汇合点（`node_roster`）、以及元素个数运行期才知道的
///     扇入（`when_all_range`）。
///
///  2. **entity 列表是动态的，依赖边是静态的**。main 持有的是
///     `::std::vector<::std::unique_ptr<basic_entity_slot>>`，谁参与本帧运行期决定；
///     但"terrain 依赖 renderer"这条边仍然是具体类型直连——terrain 手里就攥着
///     `renderer&`，通过它的 `_job_slot` 取到"本帧的 renderer job"。
///     于是插件能进来，而依赖关系不退化成字符串 / tag 查表。
///
///  3. **两阶段**。`frame` 构造时先让每个 entity 生成本帧 job（阶段 A），
///     `frame::run` 才向每个 job 要根节点（阶段 B）。阶段 A 全部做完才进阶段 B，
///     所以阶段 B 里任何 job 都能拿到任何别的 job，**注册顺序无关**。
///     唯一约定：job 的构造函数里不许访问别的 job。
///
///  4. **帧隔离是结构性的**。每帧的状态全在 job 上，`frame` 析构时逆序销毁；
///     `frame` 既不可拷贝也不可移动，`node_ref` 没有 `reset`。
///     没有"清空上一帧"这种操作，因为上一帧的东西已经不存在了。
///
///  5. **类型擦除直接用 `::exec::any_sender`**。它的第二个模板参数就是"要穿过擦除
///     边界的环境查询"清单，声明一条 `get_stop_token` 即可；库内部的
///     `_state<Receiver, inplace_stop_token>` 会自带一个 `inplace_stop_source`
///     把外层任意类型的令牌转接进来，外层令牌本身就兼容时还会走零开销特化。
///     render.end 的 fail-fast 依赖的正是这条链路。唯一的自造件是薄薄一层
///     `env_gate_sender`，用来绕开 stdexec 里 `__fwd_env_t` 按值转发环境的 bug
///     （详见该类型的注释），不是另起炉灶重写一遍类型擦除。

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <exec/any_sender_of.hpp>
#include <exec/split.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

namespace bvn::frame_graph
{

// ------------------------------------------------------------------ 原地构造

/// 存放不可移动对象的手工生命周期槽。
///
/// op-state 和 job 都是地址敏感的（子接收者 / 依赖方持有指向它的指针），既不可拷贝
/// 也不可移动，于是 `::std::optional::emplace` 这条路走不通——它是直接初始化，
/// 会去找移动构造。这里走 `::new (地址) T(工厂())`：工厂返回 T 的纯右值，
/// C++17 保证省略，对象直接落在槽里。
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

	[[nodiscard]] constexpr auto engaged() const noexcept -> bool
	{
		return _engaged;
	}
};

// ------------------------------------------------------------------ 节点句柄

/// 擦除后的接收者环境。只留停止令牌：帧图的节点不传值，错误统一是 `exception_ptr`。
struct node_env
{
	::stdexec::inplace_stop_token _stop_token;

	[[nodiscard]] constexpr auto query(::stdexec::get_stop_token_t) const noexcept -> ::stdexec::inplace_stop_token
	{
		return _stop_token;
	}
};

/// 帧图里每条边的完成形状：不传值、错误统一 `::std::exception_ptr`、可取消。
using node_completions = ::stdexec::completion_signatures<
	::stdexec::set_value_t(),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>;

/// 擦除后的接收者。第二个模板参数是**要穿过擦除边界的环境查询清单**。
///
/// 只声明 `get_stop_token` 一条：帧图的节点不传值，也不需要 domain / allocator。
/// 声明了它，`::exec::any_sender` 的 op-state 就会按外层接收者的令牌类型自动选路——
/// 外层令牌可转换成 `inplace_stop_token` 时零开销直通，否则自带一个
/// `inplace_stop_source` 把外层取消转发进来（见 any_sender_of.hpp 的
/// `_state<Receiver, inplace_stop_token>`）。render.end 的 fail-fast 要的就是这个。
using node_receiver = ::exec::any_receiver<
	node_completions,
	::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept>>;

/// 帧图里一条边的通用形态。只可移动：`node_ref` 每次交出的是一份新的擦除盒，
/// 里面装的是同一个 `split` 节点的拷贝，所以共享的是节点本身而不是这个盒子。
using node_sender = ::exec::any_sender<node_receiver>;

static_assert(::stdexec::sender<node_sender>);

/// 把环境钉死在 `node_env` 上的适配器——**规避 stdexec 的一个上游 bug**，见下。
///
/// `node_receiver` 的环境是那个多态接口本身（`any_sender_of.hpp` 里
/// `_interface_::get_env() -> _interface_ const&`），它继承自 `__any::__interface_base`，
/// 拷贝构造是 deleted，因此既不可拷贝也不可移动。而 `__continues_on.hpp:228` 拿
/// `__fwd_env_t<_Env>`（按**值**）去转发环境，落到 `__env::__fwd<_Env>` 里的
/// `static_assert(__nothrow_move_constructible<_Env>)` 上直接炸。
/// 于是"带查询的 `any_sender` + 任何含 `starts_on` / `continues_on` 的表达式"
/// 编译不过。把 `__fwd_env_t<_Env>` 改成 `__fwd_env_t<_Env const&>` 即可修复
/// （在副本上验证过），但这要改 stdexec；本地 pin 的 f91f6363 和上游 HEAD
/// 4754c76d（新 27 个提交）都还带着这个 bug。上游自己的
/// `test/exec/test_any_sender.cpp:798` 用的是完全相同的写法，只是它擦除的是
/// `just(42)`，从没擦过调度 sender，所以这个洞没被测到。
///
/// 这一层让被擦除的表达式看到的外层环境固定是 `node_env`（可平凡移动），
/// 多态接口就再也进不到 `__fwd_env_t` 里去了。停止令牌照旧穿过去：库内部的
/// `_state<Receiver, inplace_stop_token>` 先把外层令牌桥接成 `inplace_stop_token`，
/// 这里再把它读出来放进 `node_env`。完成签名写死，`any_sender` 也就不必再拿
/// 那个环境去递归推导孩子的签名。
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
			return node_env{::stdexec::get_stop_token(::stdexec::get_env(*_receiver))};
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

/// 把任意"不传值、错误是 `exception_ptr`"的 sender 装进帧图的通用边。
template <class sender_type>
[[nodiscard]] auto make_node(sender_type sender) -> node_sender
{
	return node_sender{env_gate_sender<sender_type>{::std::move(sender)}};
}

/// 本帧某个具名共享节点的备忘格。
///
/// 第一次 `get` 建图并套上 `split`，之后每次 `get` 都是订阅同一个节点。
/// 格子长在**本帧的 job** 上，所以它随帧一起死；下一帧从空格子重新建，
/// 不需要、也没有 `reset`。
///
/// 存的是"擦除工厂"而不是擦好的 `node_sender`：后者只可移动，发不出第二份。
/// 被闭包捕获的 `split` sender 是可拷贝的，每次 `get` 拷一份再擦除即可。
struct node_ref
{
	::std::function<node_sender()> _make;
	bool _building = false;

	node_ref() = default;
	node_ref(node_ref const&) = delete;
	auto operator=(node_ref const&) -> node_ref& = delete;

	/// `factory` 本帧至多被调用一次。
	template <class factory_type>
	[[nodiscard]] auto get(factory_type&& factory) -> node_sender
	{
		if (!_make)
		{
			// 建图期间又要建自己 => 构建期成环。这里能抓到的是构建期的环；
			// 运行期的环（录制节点反过来依赖 render.end）见 README「已知边界」。
			if (_building)
			{
				throw ::std::logic_error{"frame graph: 节点在构建期依赖成环"};
			}

			_building = true;

			try
			{
				_make = [shared = ::exec::split(::std::forward<factory_type>(factory)())]
				{
					return make_node(shared);
				};
			}
			catch (...)
			{
				_building = false;
				throw;
			}

			_building = false;
		}

		return _make();
	}
};

/// 启动期才被读取的动态汇合点。
///
/// 参与者在**构建期**把自己的节点挂进来，汇合点在**启动期**（`let_value` 体里）
/// 才把名单封存。stdexec 的惰性模型免费给了这条分界线：整张图在 `connect` 之前
/// 就已经构建完毕，所以谁先挂谁后挂完全无关。挂晚了则是硬错误，不是静默丢失。
struct node_roster
{
	::std::vector<node_sender> _nodes;
	bool _sealed = false;

	auto add(node_sender node) -> void
	{
		if (_sealed)
		{
			throw ::std::logic_error{"frame graph: 注册晚于汇合点封存"};
		}

		_nodes.push_back(::std::move(node));
	}

	[[nodiscard]] auto seal() -> ::std::vector<node_sender>
	{
		_sealed = true;
		return ::std::move(_nodes);
	}

	[[nodiscard]] constexpr auto sealed() const noexcept -> bool
	{
		return _sealed;
	}

	[[nodiscard]] constexpr auto size() const noexcept -> ::std::size_t
	{
		return _nodes.size();
	}
};

// -------------------------------------------------------------- 动态扇入

/// 元素个数到启动期才知道的 `when_all`。
///
/// 为什么不是"逐个 spawn 进 async_scope"：spawn 丢错误、丢取消、每个孩子一次堆分配，
/// 而 `scope.on_empty()` 是"空了"不是"汇合"——错误和取消都传不出来。
/// 这里是一个真正的汇合子：一个倒计数器 + 首个非正常完成胜出 + fail-fast 广播取消。
///
/// 孩子是同构的 `node_sender` 且不传值，所以完成签名可以写死，
/// 不需要 `transform_completion_signatures`，也不需要折叠值 variant。
struct when_all_range_sender
{
	using sender_concept = ::stdexec::sender_t;
	using completion_signatures = ::stdexec::completion_signatures<
		::stdexec::set_value_t(),
		::stdexec::set_error_t(::std::exception_ptr),
		::stdexec::set_stopped_t()>;

	enum class completion_kind
	{
		value,
		error,
		stopped,
	};

	template <class receiver_type>
	struct operation
	{
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

		using child_operation_type = ::stdexec::connect_result_t<node_sender, child_receiver>;
		using outer_token_type = ::stdexec::stop_token_of_t<::stdexec::env_of_t<receiver_type>>;
		using stop_callback_type = typename outer_token_type::template callback_type<forward_stop>;

		receiver_type _receiver;
		::std::vector<node_sender> _sources;
		::std::size_t _count = 0;
		::std::unique_ptr<manual_lifetime<child_operation_type>[]> _operations;
		::std::atomic<::std::size_t> _pending{0};
		::std::atomic<completion_kind> _kind{completion_kind::value};
		::std::exception_ptr _error;
		::stdexec::inplace_stop_source _stop_source;
		::std::optional<stop_callback_type> _on_stop;

		operation(::std::vector<node_sender> sources, receiver_type receiver)
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

		auto fail(::std::exception_ptr error) noexcept -> void
		{
			auto expected = completion_kind::value;
			if (_kind.compare_exchange_strong(expected, completion_kind::error, ::std::memory_order_acq_rel))
			{
				_error = ::std::move(error);
				_stop_source.request_stop();
			}
		}

		auto stopped() noexcept -> void
		{
			auto expected = completion_kind::value;
			if (_kind.compare_exchange_strong(expected, completion_kind::stopped, ::std::memory_order_acq_rel))
			{
				_stop_source.request_stop();
			}
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

	::std::vector<node_sender> _children;

	// 单发射：孩子是只可移动的 `node_sender`，连接即消费。`let_value` 以右值连接它
	// 返回的 sender（__let.hpp 里 `__nothrow_connectable<__sndr2_t, __rcvr2_t>` 用的是
	// 非引用类型），顶层 `frame::run` 也是就地构造后立刻连，两处都够用。
	template <::stdexec::receiver receiver_type>
	[[nodiscard]] auto connect(receiver_type receiver) && -> operation<receiver_type>
	{
		return operation<receiver_type>{::std::move(_children), ::std::move(receiver)};
	}
};

[[nodiscard]] inline auto when_all_range(::std::vector<node_sender> children) -> when_all_range_sender
{
	return when_all_range_sender{._children = ::std::move(children)};
}

static_assert(::stdexec::sender<when_all_range_sender>);

// ------------------------------------------------------------------ entity

/// 一帧对所有 entity 广播的只读上下文。
///
/// 它是 job 唯一需要的"世界"——不含 entity 列表，于是 entity 之间不必互相知道，
/// 也不会因为 main 换了收集方式而重新编译。
struct frame_context
{
	::exec::static_thread_pool::scheduler _scheduler;
	::std::uint64_t _index = 0;
	float _delta_seconds = 0.0f;
	::std::atomic<bool>* _quit = nullptr;

	/// 本帧的取消令牌。
	///
	/// **取消不走接收者环境，走这里。** 原因是 `::exec::split` 在订阅者的令牌已经
	/// 停止时会直接 `set_stopped`、根本不启动共享体（probe 验证过）。而 fence 节点
	/// 本身就是一个 `split`——如果取消沿着图的结构边往下压，整帧被取消时 fence 的
	/// 清理体一次都不会跑，GPU 资源就漏了。
	///
	/// 于是这里把两件事拆开：图的**结构边**永不取消（根接收者拿的是一个永不停止的
	/// 令牌），而取消由各个**干活的节点**自己用 `cancellable` 从帧上下文取。
	::stdexec::inplace_stop_token _stop_token;

	auto request_quit() const noexcept -> void
	{
		_quit->store(true, ::std::memory_order_relaxed);
	}
};

/// 把"本帧的取消令牌"注入一段表达式——干活的节点用它来变得可取消。
///
/// 注入而不是继承：结构边上流的是永不停止的令牌（见 `frame_context::_stop_token`），
/// 所以想响应取消的节点必须显式说出来。`write_env` 写进去的值优先于外层。
template <class sender_type>
[[nodiscard]] auto cancellable(sender_type sender, frame_context const& context)
{
	return ::stdexec::write_env(
		::std::move(sender),
		::stdexec::prop{::stdexec::get_stop_token, context._stop_token});
}

/// 一个 job 只需要能交出本帧的根节点。
template <class job_type>
concept frame_job = requires (job_type& job)
{
	{ job.frame_node() } -> ::std::same_as<node_sender>;
};

/// 一个 entity 只需要能生成本帧的 job。
///
/// 注意这里**没有**任何 render 相关的东西：纯计算 entity 满足这个 concept 时，
/// 它的头文件里连 renderer 都不会出现。
template <class entity_type>
concept frame_entity = requires (entity_type& entity, frame_context const& context)
{
	{ entity.begin_frame(context) };
};

template <frame_entity entity_type>
using job_of = decltype(::std::declval<entity_type&>().begin_frame(::std::declval<frame_context const&>()));

/// 把"本帧的我"暴露给依赖方的槽位。
///
/// 只有**会被别的 entity 依赖**的 entity 才需要放一个。依赖方手里已经有对端的
/// 具体类型引用（`renderer&`），通过这个槽就能拿到"本帧的 renderer job"，
/// 于是不再需要 main 提供一张 entity 表来查。
template <class job_type>
struct job_slot
{
	job_type* _job = nullptr;

	/// 强依赖：对端本帧不在场就是非法状态。
	[[nodiscard]] auto get() const -> job_type&
	{
		if (_job == nullptr)
		{
			throw ::std::logic_error{"frame graph: 依赖的 entity 未参与本帧"};
		}

		return *_job;
	}

	/// 弱依赖：对端可能不在场，由调用方决定怎么办。
	[[nodiscard]] constexpr auto get_if() const noexcept -> job_type*
	{
		return _job;
	}
};

template <class entity_type>
concept exposes_current_job = frame_entity<entity_type>
	&& requires (entity_type& entity)
	{
		{ entity._job_slot } -> ::std::same_as<job_slot<job_of<entity_type>>&>;
	};

/// 动态 entity 列表里的一格。main 只跟这个接口打交道。
struct basic_entity_slot
{
	virtual ~basic_entity_slot() = default;

	/// 阶段 A：生成本帧 job。此时别的 job 可能还不存在，不许访问它们。
	virtual auto begin_frame(frame_context const& context) -> void = 0;
	/// 阶段 B：交出本帧的根节点。此时所有 job 都已就位。
	[[nodiscard]] virtual auto frame_node() -> node_sender = 0;
	virtual auto end_frame() noexcept -> void = 0;
	[[nodiscard]] virtual auto entity_address() const noexcept -> void const* = 0;
};

template <frame_entity entity_type>
struct entity_slot final : basic_entity_slot
{
	using job_type = job_of<entity_type>;

	entity_type* _entity = nullptr;
	manual_lifetime<job_type> _job;

	explicit entity_slot(entity_type& entity) noexcept
		: _entity(&entity)
	{}

	auto begin_frame(frame_context const& context) -> void override
	{
		auto&& job = _job.construct([&] { return _entity->begin_frame(context); });

		if constexpr (exposes_current_job<entity_type>)
		{
			_entity->_job_slot._job = &job;
		}
	}

	[[nodiscard]] auto frame_node() -> node_sender override
	{
		return _job.get().frame_node();
	}

	auto end_frame() noexcept -> void override
	{
		if constexpr (exposes_current_job<entity_type>)
		{
			// 先摘指针再销毁：帧后误用 `job()` 得到的是异常，不是悬垂引用。
			_entity->_job_slot._job = nullptr;
		}

		_job.reset();
	}

	[[nodiscard]] auto entity_address() const noexcept -> void const* override
	{
		return _entity;
	}
};

// ------------------------------------------------------------- world / frame

/// entity 的注册表 + 跨帧的全局量。
///
/// 名单是运行期的：`add` / `remove` 只能在帧与帧之间调用，这正好跟"每帧重建图"对齐，
/// 不需要额外的锁或延迟队列。
struct world
{
	::exec::static_thread_pool* _pool = nullptr;
	::std::vector<::std::unique_ptr<basic_entity_slot>> _slots;
	::std::atomic<bool> _quit{false};
	::std::uint64_t _frame_index = 0;
	float _delta_seconds = 1.0f / 60.0f;

	explicit world(::exec::static_thread_pool& pool) noexcept
		: _pool(&pool)
	{}

	world(world const&) = delete;
	auto operator=(world const&) -> world& = delete;

	template <frame_entity entity_type>
	auto add(entity_type& entity) -> void
	{
		assert(!contains(entity));   // 同一个 entity 注册两遍是逻辑 bug
		_slots.push_back(::std::make_unique<entity_slot<entity_type>>(entity));
	}

	template <class entity_type>
	auto remove(entity_type const& entity) noexcept -> bool
	{
		auto const address = static_cast<void const*>(&entity);
		for (auto index = ::std::size_t{0}; index != _slots.size(); ++index)
		{
			if (_slots[index]->entity_address() == address)
			{
				_slots.erase(_slots.begin() + static_cast<::std::ptrdiff_t>(index));
				return true;
			}
		}

		return false;
	}

	template <class entity_type>
	[[nodiscard]] auto contains(entity_type const& entity) const noexcept -> bool
	{
		auto const address = static_cast<void const*>(&entity);
		for (auto&& slot : _slots)
		{
			if (slot->entity_address() == address)
			{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] auto context() noexcept -> frame_context
	{
		return frame_context{
			._scheduler = _pool->get_scheduler(),
			._index = _frame_index,
			._delta_seconds = _delta_seconds,
			._quit = &_quit,
			._stop_token = {},  // 由 `frame` 的构造函数填上本帧的取消源。
		};
	}

	[[nodiscard]] auto quit_requested() const noexcept -> bool
	{
		return _quit.load(::std::memory_order_relaxed);
	}

	auto advance() noexcept -> void
	{
		++_frame_index;
	}
};

/// 一帧的作用域。
///
/// 构造 = 阶段 A（每个 entity 生成本帧 job），`run` = 阶段 B（构图并执行），
/// 析构 = 逆序销毁全部 job。既不可拷贝也不可移动：帧与帧之间的隔离是结构性的，
/// 不靠"记得调用 reset"这种纪律。
struct frame
{
	world* _world = nullptr;

	/// 本帧的取消源。外部令牌一停就转发到这里，再由 `cancellable` 分发给干活的节点。
	::stdexec::inplace_stop_source _stop_source;

	/// 结构边用的令牌源，**从不 request_stop**。图的骨架不参与取消，
	/// 否则 `split` 会在订阅侧短路，fence 的清理就没了。
	::stdexec::inplace_stop_source _structural_source;

	frame_context _context;

	explicit frame(world& target)
		: _world(&target)
		, _context(target.context())
	{
		_context._stop_token = _stop_source.get_token();

		for (auto&& slot : _world->_slots)
		{
			slot->begin_frame(_context);
		}
	}

	frame(frame const&) = delete;
	auto operator=(frame const&) -> frame& = delete;

	~frame()
	{
		for (auto index = _world->_slots.size(); index != 0; --index)
		{
			_world->_slots[index - 1]->end_frame();
		}
	}

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

	/// 阶段 B：向每个 entity 要根节点，汇成一张图跑完。
	///
	/// 返回 false 表示整帧被取消。图内的错误以异常抛出。
	/// 这里手写了一个 `sync_wait`，只为了能把外部停止令牌塞进接收者环境——
	/// 有了它才能测"上层取消整帧时 fence 仍然执行"。
	auto run(::stdexec::inplace_stop_token stop_token = {}) -> bool
	{
		auto roots = ::std::vector<node_sender>{};
		roots.reserve(_world->_slots.size());
		for (auto&& slot : _world->_slots)
		{
			roots.push_back(slot->frame_node());
		}

		// 外部取消转发进本帧的取消源。已经停止的令牌在这里注册即刻回调，
		// 于是图还没启动就已经是"取消态"了。
		auto forward = ::std::optional<::stdexec::inplace_stop_callback<forward_stop>>{};
		forward.emplace(stop_token, forward_stop{&_stop_source});

		// 根接收者拿的是结构令牌（永不停止），取消只从 `cancellable` 那条路进图。
		auto state = run_state{._error = {}, ._stop_token = _structural_source.get_token()};
		auto operation = ::stdexec::connect(when_all_range(::std::move(roots)), run_receiver{&state});
		::stdexec::start(operation);
		state._done.acquire();

		if (state._error)
		{
			::std::rethrow_exception(state._error);
		}

		return !state._stopped;
	}
};

}
