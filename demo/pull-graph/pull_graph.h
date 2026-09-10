#pragma once

/// 每帧重建的任务图 —— 需求驱动的最小实现。
///
/// 图就是 sender 表达式。这里只有四件东西：
///
/// | 原语 | 干什么 |
/// | --- | --- |
/// | `node_sender` | 一条边（类型擦除）。**只在扇入边界用**，别处都是具体类型 |
/// | `dynamic_when_all` | 元素个数运行期才知道的扇入。`when_all` 是变参包，凑不出来 |
/// | `frame_context::get<T>(owner)` | 本帧按 (owner, 类型) 惰性物化 + memo + 环检测 |
/// | `run_frame` | 取走根节点，连接、启动、等完成 |
///
/// 扇出用 `::exec::split`、取消用 `::stdexec::write_env`、汇合点用
/// `::std::vector<node_sender>`——这三样直接用，不包一层。
///
/// **构建顺序问题由 `get` 解决**：谁需要谁触发物化，拓扑序从递归里长出来，不排序、
/// 不分阶段、没有 `build()`。T 的构造函数里就把事情做完：拉依赖、建节点、
/// `context.add(根节点)`。递归回到正在物化的条目就是环，当场抛。
///
/// **T 装多少东西由使用者划**，库不区分粗细：
///   - 帧状态耦合的放一个 T（一个节点写、另一个节点读的那种），兄弟节点之间是成员访问；
///   - 跨 entity 共享、又没有帧状态的单条边，自己一个 T。
/// 两者都是 `get<T>(owner)`。
///
/// **取消不走结构边。** `::exec::split` 在订阅者令牌已停止时直接 `set_stopped`、根本
/// 不启动共享体，所以取消若沿结构边下压，收尾节点就一次都不跑。这里结构边**完全**
/// 不传取消：根接收者和扇入的孩子拿的都是默认构造的（永不停止的）令牌，取消只从
/// 干活的叶子节点自己 `write_env(表达式, prop{get_stop_token, context._stop_token})`
/// 那条路进图。
///
/// **不管的事**：调度器与帧数据（从 `frame_context` 派生自己加）、entity 名单
/// （是调用者的）、汇合点要不要防"挂晚了"（是使用者的，库只给裸容器）。

#include <atomic>
#include <cassert>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <semaphore>
#include <stdexcept>
#include <utility>
#include <vector>

#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

namespace bvn::pull_graph
{

// ------------------------------------------------------------------ 原地构造

/// 存放不可移动对象的手工生命周期槽。
///
/// op-state 是地址敏感的（子接收者持有指向它的指针），既不可拷贝也不可移动，于是
/// `::std::optional::emplace` 这条路走不通——它是直接初始化，会去找移动构造。
/// 这里走 `::new (地址) T(工厂())`：工厂返回 T 的纯右值，C++17 保证省略。
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

/// 扇入边界上每条边的完成形状：不传值、错误统一 `::std::exception_ptr`、可取消。
using node_completions = ::stdexec::completion_signatures<
	::stdexec::set_value_t(),
	::stdexec::set_error_t(::std::exception_ptr),
	::stdexec::set_stopped_t()>;

/// 节点看到的环境。只留停止令牌。默认构造出来的令牌永不停止，正好当结构边用。
struct node_env
{
	::stdexec::inplace_stop_token _stop_token;

	[[nodiscard]] constexpr auto query(::stdexec::get_stop_token_t) const noexcept -> ::stdexec::inplace_stop_token
	{
		return _stop_token;
	}
};

/// 擦除后的接收者。第二个模板参数是要穿过擦除边界的环境查询清单。
using node_receiver = ::exec::any_receiver<
	node_completions,
	::exec::queries<::stdexec::inplace_stop_token(::stdexec::get_stop_token_t) noexcept>>;

/// 扇入边界上一条边的通用形态。只可移动：连接即消费，一条边本帧只跑一次。
using node_sender = ::exec::any_sender<node_receiver>;

static_assert(::stdexec::sender<node_sender>);

/// 把环境钉死在 `node_env` 上的适配器——**规避 stdexec 的一个上游 bug**。
///
/// `node_receiver` 的环境是那个多态接口本身（`_interface_::get_env()` 返回
/// `_interface_ const&`），它继承自 `__any::__interface_base`，拷贝构造是 deleted，
/// 因此不可移动。而 `__continues_on.hpp:228` 拿 `__fwd_env_t<_Env>`（按**值**）去
/// 转发环境，落到 `__env::__fwd<_Env>` 的
/// `static_assert(__nothrow_move_constructible<_Env>)` 上直接炸。于是"带查询的
/// `any_sender` + 任何含 `starts_on` / `continues_on` 的表达式"编译不过。本地 pin 的
/// f91f6363 和上游 HEAD 4754c76d 都还带着它。
///
/// 这一层让被擦除的表达式看到的外层环境固定是 `node_env`（可平凡移动），多态接口就
/// 进不到 `__fwd_env_t` 里去了。完成签名写死，`any_sender` 也不必再拿那个环境去
/// 递归推导孩子的签名。停止令牌照旧穿过。
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

/// 把一个"不传值、错误是 `::std::exception_ptr`"的 sender 装成扇入边界上的一条边。
///
/// 共享节点先 `::exec::split`，再把 split sender 的**拷贝**喂给这里：split sender
/// 可拷贝，`node_sender` 只可移动，所以每个消费者拿一份新的擦除盒，共享的是节点本身。
template <class sender_type>
[[nodiscard]] auto make_node(sender_type sender) -> node_sender
{
	return node_sender{env_gate_sender<sender_type>{::std::move(sender)}};
}

// ---------------------------------------------------------------- 动态扇入

/// 元素个数到启动期才知道的 `when_all`。
///
/// 跟 `::stdexec::when_all` 有两处语义差别，都是有意的：
///
///  1. **孩子出错 / 被取消时不广播取消给兄弟。** 兄弟里但凡有个收尾节点（`split`
///     出来的清理边），被广播停掉就在订阅侧短路，收尾体一次都不跑；而谁先完成是
///     竞态，于是漏不漏收尾也成了竞态。这里选 correctness over fail-fast：
///     所有孩子跑完，第一个非正常完成的结果胜出。
///  2. **不把外层的取消请求转给孩子。** 结构边完全不传取消，取消只从叶子节点自己
///     `write_env` 那条路进图。
///
/// 孩子同构且不传值，所以完成签名写死，不需要 `transform_completion_signatures`。
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
				return node_env{};
			}
		};

		using child_operation_type = ::stdexec::connect_result_t<child_sender, child_receiver>;

		receiver_type _receiver;
		::std::vector<child_sender> _sources;
		::std::size_t _count = 0;
		::std::unique_ptr<manual_lifetime<child_operation_type>[]> _operations;
		::std::atomic<::std::size_t> _pending{0};
		::std::atomic<completion_kind> _kind{completion_kind::value};
		::std::exception_ptr _error;

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

/// 每个类型一个唯一地址，给托管的条目打类型标签用，不需要 RTTI。
template <class type>
inline constexpr char entry_tag = 0;

/// 一帧的上下文：按 (owner, 类型) 惰性物化并托管任意东西，收根节点，带取消令牌。
///
/// 调度器、帧号、时间步这些**不在这里**——它们是使用者的帧数据，派生一个自己的
/// 上下文加上去即可：`struct context : frame_context<context> { ... };`。
/// CRTP 是必要的：物化 T 时要把**派生后的**上下文传给它的构造函数，
/// 否则 T 读不到那些帧数据。
template <class self_type>
struct frame_context
{
	struct entry
	{
		void const* _owner = nullptr;
		void const* _tag = nullptr;
		void* _object = nullptr;                 // 物化中是 nullptr —— 递归撞上就是环
		void (*_destroy)(void*) = nullptr;
	};

	::std::vector<entry> _entries;
	::std::vector<node_sender> _roots;

	/// 干活的节点自己写 `write_env(表达式, prop{get_stop_token, _stop_token})`。
	::stdexec::inplace_stop_token _stop_token;

	frame_context() = default;
	frame_context(frame_context const&) = delete;
	auto operator=(frame_context const&) -> frame_context& = delete;

	~frame_context()
	{
		// 逆序销毁：后物化的引用先物化的。
		for (auto index = _entries.size(); index != 0; --index)
		{
			auto&& target = _entries[index - 1];
			if (target._object != nullptr)
			{
				target._destroy(target._object);
			}
		}
	}

	/// 本帧这个 owner 的 T：没有就现在造，造过返回同一个，正在造就是环。
	///
	/// `owner` 传 entity 自己的地址即可——依赖方手里攥着的正是那个 entity 的引用，
	/// 于是依赖关系仍然是具体类型直连，不退化成字符串 / tag 查表。
	template <class value_type, class owner_type>
	auto get(owner_type& owner) -> value_type&
	{
		auto const key = static_cast<void const*>(&owner);

		for (auto&& target : _entries)
		{
			if (target._owner != key || target._tag != &entry_tag<value_type>)
			{
				continue;
			}

			if (target._object == nullptr)
			{
				throw ::std::logic_error{"pull graph: 物化期依赖成环"};
			}

			return *static_cast<value_type*>(target._object);
		}

		// 先占位再构造。构造过程里递归回到这里会看到 `_object == nullptr`，即环。
		// 用下标而不是引用/指针：递归会往 `_entries` 里 push，容器可能重分配。
		// （条目里存的是**堆上对象的指针**，重分配不会移动对象，所以已经发出去的
		//  `value_type&` 仍然有效——这是存指针而不是存对象的必要理由。）
		_entries.push_back(entry{._owner = key, ._tag = &entry_tag<value_type>});
		auto const index = _entries.size() - 1;

		auto owned = ::std::make_unique<value_type>(owner, static_cast<self_type&>(*this));

		_entries[index]._object = owned.get();
		_entries[index]._destroy = [](void* object) { delete static_cast<value_type*>(object); };

		return *owned.release();
	}

	/// 把一条根节点挂进本帧。挂几条、挂不挂，由物化出来的那个 T 自己决定。
	auto add(node_sender node) -> void
	{
		_roots.push_back(::std::move(node));
	}
};

// ------------------------------------------------------------------ 跑一帧

/// 取走根节点，连接、启动、等完成。返回 false 表示整帧被取消；图内的错误以异常抛出。
///
struct run_state
{
	::std::binary_semaphore _done{0};
	::std::exception_ptr _error;
	bool _stopped = false;
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

	/// 默认构造的令牌永不停止——结构边不传取消。
	[[nodiscard]] auto get_env() const noexcept -> node_env
	{
		return node_env{};
	}
};

/// 手写了一个 `sync_wait`，只为了给根接收者一个**永不停止**的令牌：取消必须由节点
/// 自己 `write_env` 注入，不能从结构边压下来。
template <class self_type>
auto run_frame(frame_context<self_type>& context) -> bool
{
	auto state = run_state{};
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
