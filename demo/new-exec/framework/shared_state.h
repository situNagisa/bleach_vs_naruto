#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

/// 帧内共享状态：entity 之间**只共享数据，不共享 sender**，所以执行图始终是静态的。
///
/// | 时机 | 发生什么 |
/// | --- | --- |
/// | connect | `publish` 的 operation_state 构造 `Shared`，登记进 env 里的 registry |
/// | start 之后 | 别人用 `find_shared<T>(registry, instance)` 取到 `T*` |
/// | 阶段结束 | `Shared` 随 operation_state 析构；registry 由场景清空 |
///
/// **前提（由场景的结构保证）**：所有要互相访问的 entity 挂在**同一个** `when_all` /
/// `when_all_range` 下面。它们先把全部子节点 connect 完再逐个 start，于是
/// "任何人开始执行之前，所有人都已登记"，不需要排序也不需要额外同步；
/// 子 operation_state 要等整个 when_all 析构才销毁，所以先跑完的人的状态别人照样能读。
///
/// **`publish` 必须在最外层。** `starts_on` / `let_value` 都是在运行时才 connect 内层，
/// 包在它们里面就退化成"跑到了才登记"，别人可能查不到。调度放进 factory 里面做。
///
/// 访问方必须知道对方的共享类型（include 对方的 shared 头）；查不到返回 nullptr，
/// 由访问方自己决定降级。这里不规定共享类型里放什么——快照、inbox、atomic 都由 entity 作者定。

/// FNV-1a。类型 id 由作者给出稳定字符串，不用 typeid / entt hash：跨 dll、跨编译器都可靠。
[[nodiscard]] constexpr auto stable_hash(::std::string_view text) noexcept -> ::std::uint64_t
{
	auto hash = ::std::uint64_t{ 0xcbf29ce484222325ull };
	for (auto character : text)
	{
		hash ^= static_cast<unsigned char>(character);
		hash *= 0x100000001b3ull;
	}
	return hash;
}

struct shared_key
{
	::std::uint64_t type;      ///< `Shared::shared_type_id`
	::std::uint32_t instance;  ///< 场景在 spawn 时分配
};

/// registry 只规定两个操作；存储归场景（span、定长数组、哈希表都行）。
template <class Registry>
concept shared_registry = requires (Registry& registry, shared_key key, void* state)
{
	registry.publish(key, state);
	{ registry.find(key) } -> ::std::convertible_to<void*>;
};

/// 共享类型要求：带一个稳定的类型 id。
template <class Shared>
concept shareable = requires
{
	{ Shared::shared_type_id } -> ::std::convertible_to<::std::uint64_t>;
};

struct get_shared_registry_t
{
	template <class Env>
		requires requires (Env const& env) { env.query(get_shared_registry_t{}); }
	constexpr auto operator()(Env const& env) const noexcept
	{
		return env.query(*this);   // 返回 registry 指针
	}
};
inline constexpr get_shared_registry_t get_shared_registry{};

template <class Env>
using shared_registry_of_t = ::std::remove_pointer_t<::std::invoke_result_t<get_shared_registry_t, Env const&>>;

/// 类型化访问。对方本帧不在（没 spawn、dll 没加载）就是 nullptr。
template <shareable Shared>
[[nodiscard]] auto find_shared(shared_registry auto& registry, ::std::uint32_t instance) -> Shared*
{
	return static_cast<Shared*>(registry.find({ Shared::shared_type_id, instance }));
}

/// `publish(instance, init, factory)`
///
/// - `init()` 返回 `Shared` 的纯右值，在 operation_state 里就地构造（`Shared` 可以不可移动，
///   比如带 atomic 的 inbox）。这时谁都还没开始跑，适合写"本帧只读"的快照。
/// - `factory(Shared&, Registry&)` 返回 entity 本帧真正要跑的 sender。
template <class Init, class Factory>
struct publish_sender
{
	using sender_concept = ::stdexec::sender_t;
	using shared_type = ::std::invoke_result_t<Init&>;

	template <class Env>
	using _inner_t = ::std::invoke_result_t<Factory&, shared_type&, shared_registry_of_t<Env>&>;

	// registry 的类型来自 env，所以这是个依赖型 sender：补全签名就是 factory 产出的那个 sender 的。
	// （写法随 stdexec 版本不同，这里用的是新版的静态 consteval 形式。）
	template <class Self, class Env>
	static consteval auto get_completion_signatures()
	{
		return ::stdexec::get_completion_signatures<_inner_t<Env>, Env>();
	}

	::std::uint32_t _instance;
	Init _init;
	Factory _factory;

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ::stdexec::operation_state_t;
		using _inner_operation_t = ::stdexec::connect_result_t<_inner_t<::stdexec::env_of_t<Receiver>>, Receiver>;

		// 声明顺序即构造顺序：`_shared` 先就位，`_inner` 构造时才能拿它的地址。
		shared_type _shared;
		_inner_operation_t _inner;

		operation(publish_sender&& self, Receiver receiver)
			: _shared(self._init())
			, _inner(_connect(self, ::std::move(receiver)))
		{}

		operation(operation&&) = delete;
		auto operator=(operation&&) -> operation& = delete;

		void start() & noexcept
		{
			::stdexec::start(_inner);
		}

		auto _connect(publish_sender& self, Receiver receiver) -> _inner_operation_t
		{
			auto& registry = *get_shared_registry(::stdexec::get_env(receiver));
			// connect 发生在启动线程上、所有 start 之前；别人在池线程上读它，
			// 中间隔着 scheduler 的入队/出队，happens-before 由调度保证。
			registry.publish({ shared_type::shared_type_id, self._instance }, &_shared);
			return ::stdexec::connect(self._factory(_shared, registry), ::std::move(receiver));
		}
	};

	template <class Receiver>
	auto connect(Receiver receiver) && -> operation<Receiver>
	{
		return operation<Receiver>{ ::std::move(*this), ::std::move(receiver) };
	}
};

template <class Init, class Factory>
	requires shareable<::std::invoke_result_t<Init&>>
[[nodiscard]] auto publish(::std::uint32_t instance, Init init, Factory factory)
{
	return publish_sender<Init, Factory>{ instance, ::std::move(init), ::std::move(factory) };
}

// ---------------------------------------------------------------- 可选的现成部件
// 下面两样只是"一种"实现，场景/entity 可以换成自己的。

/// 按 instance 下标的 registry。**不拥有存储**：存储由场景给，只在拓扑变化时调整大小。
/// 每个 instance 只写自己的槽，所以 publish 不加锁。
struct span_registry
{
	struct entry
	{
		::std::uint64_t type = 0;
		void* state = nullptr;
	};

	::std::span<entry> _entries{};

	void rebind(::std::span<entry> entries) noexcept
	{
		_entries = entries;
		reset();
	}

	void reset() noexcept
	{
		::std::ranges::fill(_entries, entry{});
	}

	void publish(shared_key key, void* state) noexcept
	{
		assert(key.instance < _entries.size() && "shared_state: instance 超出 registry 容量");
		assert(_entries[key.instance].state == nullptr && "shared_state: 同一 instance 本帧重复登记");
		_entries[key.instance] = { key.type, state };
	}

	[[nodiscard]] auto find(shared_key key) const noexcept -> void*
	{
		if (key.instance >= _entries.size())
			return nullptr;
		auto const& found = _entries[key.instance];
		return found.type == key.type ? found.state : nullptr;
	}
};

/// 定长、无锁、多写者的事件收件箱。满了就丢（push 返回 false），容量由 entity 作者定。
/// `events()` 只能在所有写者都结束之后调用——也就是包住它们的 when_all 完成之后。
template <class Event, ::std::size_t Capacity>
struct fixed_inbox
{
	Event _events[Capacity]{};
	::std::atomic<::std::size_t> _count{ 0 };

	auto push(Event const& event) noexcept -> bool
	{
		auto const index = _count.fetch_add(1, ::std::memory_order_relaxed);
		if (index >= Capacity)
			return false;
		_events[index] = event;
		return true;
	}

	[[nodiscard]] auto events() const noexcept -> ::std::span<Event const>
	{
		return { _events, (::std::min)(_count.load(::std::memory_order_relaxed), Capacity) };
	}
};
