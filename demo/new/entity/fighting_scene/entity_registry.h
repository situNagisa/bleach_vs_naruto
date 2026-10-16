#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

/// 帧内共享状态的登记表。entity 之间**只共享数据，不共享 sender**，执行图始终是静态的。
///
/// 每帧开头场景同步地让所有 entity `publish` 一遍（按 instance 序），然后 `seal`，
/// 之后才进入并发阶段。所以"登记早于访问"由一个显式的同步步骤保证，跟适配器在
/// connect 时连不连孩子无关。被登记的对象是 entity 的成员，地址在整帧内稳定。
///
/// 访问方必须知道对方的共享类型（include 对方的 shared 头）。拿到的是 `T const*`：
/// 别人只能读；允许别人写的通道（inbox 之类）由类型作者声明成 `mutable`。

/// FNV-1a。类型 id 由作者给稳定字符串，不用 typeid：跨 dll 可靠。
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

template <class Shared>
concept shareable = requires
{
	{ Shared::shared_type_id } -> ::std::convertible_to<::std::uint64_t>;
};

struct entity_registry
{
	struct entry
	{
		::std::uint64_t type;
		::std::uint32_t instance;
		void const* state;
	};

	/// 存储归场景；`clear` 保留容量，热身之后每帧不分配。
	::std::vector<entry> _entries{};
	bool _sealed = false;

	/// 布局也编进键里：两个 dll 用了不同版本的 shared 头却忘了改 id，查不到而不是 UB。
	template <shareable Shared>
	[[nodiscard]] static constexpr auto key() noexcept -> ::std::uint64_t
	{
		return Shared::shared_type_id ^ (static_cast<::std::uint64_t>(sizeof(Shared)) << 48) ^ (static_cast<::std::uint64_t>(alignof(Shared)) << 40);
	}

	void clear() noexcept
	{
		_entries.clear();
		_sealed = false;
	}

	void seal() noexcept { _sealed = true; }

	/// 一个 entity 可以发布多个共享对象（比如自己的状态 + 一个物理刚体）。
	template <shareable Shared>
	void publish(::std::uint32_t instance, Shared const& state)
	{
		assert(!_sealed && "entity_registry: 并发阶段里不能再登记");
		_entries.push_back({ key<Shared>(), instance, &state });
	}

	/// 点查：知道对方是谁。
	template <shareable Shared>
	[[nodiscard]] auto find(::std::uint32_t instance) const noexcept -> Shared const*
	{
		assert(_sealed && "entity_registry: 登记阶段还没结束");
		for (auto const& found : _entries)
		{
			if (found.type == key<Shared>() && found.instance == instance)
				return static_cast<Shared const*>(found.state);
		}
		return nullptr;
	}

	/// 枚举：所有发布了 `Shared` 的 entity，按 instance 序（结果确定）。
	template <shareable Shared, class Visit>
	void each(Visit&& visit) const
	{
		assert(_sealed && "entity_registry: 登记阶段还没结束");
		for (auto const& found : _entries)
		{
			if (found.type == key<Shared>())
				visit(found.instance, *static_cast<Shared const*>(found.state));
		}
	}
};

/// 定长、无锁、多写者的收件箱。放进共享类型时声明成 `mutable`。
///
/// - update 阶段：别人 `push`（满了就丢，返回 false）
/// - settle 阶段：拥有者 `events()` 读、排序、`clear()`。阶段之间的 when_all 屏障提供 happens-before。
///
/// 写入顺序随线程调度而变；要确定性就在 settle 里按 attacker 之类的键排序后再应用。
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

	[[nodiscard]] auto events() noexcept -> ::std::span<Event>
	{
		return { _events, (::std::min)(_count.load(::std::memory_order_relaxed), Capacity) };
	}

	void clear() noexcept { _count.store(0, ::std::memory_order_relaxed); }
};
