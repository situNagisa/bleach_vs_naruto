#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>


/// entity / task 两层结构。**这个头不认识 sender**——根节点容器、停止令牌、scheduler
/// 全在使用者自己的上下文里，这里只按模板参数把它转发给 entity。
///
/// | 概念 | 是什么 |
/// | --- | --- |
/// | entity | 跨帧存活的游戏对象。按类型查找，本帧可能不参与 |
/// | task | 挂在某个 entity 下、按类型索引的本帧任务 |
///
/// 一个 entity 的全部 task 由它自己的 `build_task` **一次性**登记，所以"构建过没有"
/// 是每 entity 一个标志，不是每 task 一个。
///
/// **构建顺序不需要排**：谁要读别人的 task，谁就去查对方、（显式或隐式）触发对方构建、
/// 再读。拓扑序从递归里长出来。递归回到正在构建的 entity 就是环，当场断言。


/// 已构建完的 entity 的只读视图：只能取 task，不能再登记。
struct built_entity
{
	::entt::registry::context* _tasks;

	/// 这个 entity 登记过的 `TaskType`；没登记过返回 `nullptr`。
	template <class TaskType>
	[[nodiscard]] auto task() const -> TaskType*
	{
		return _tasks->find<TaskType>();
	}
};

/// 写入视图：只在 `build_task` 期间有效，用来登记本 entity 的 task。
struct task_builder
{
	::entt::registry::context* _tasks;

	/// 原地构造一个 task。`TaskType` 可以是不可移动的——`::entt::basic_any<0u>` 一律走堆，
	/// 对象地址在整帧内稳定，所以 task 之间可以互相持引用。
	///
	/// **task 里带 `::std::vector<只可移动的东西>` 的话，请显式 delete 它的移动构造。**
	/// `::std::vector` 的拷贝构造**永远**是声明着的（不管元素可不可拷贝），于是
	/// `::std::is_copy_constructible_v<TaskType>` 误判成 true，`::entt::basic_any` 就会去
	/// 实例化那条拷贝路径，错误炸在 vector 内部、离真正的原因隔着几十层模板。
	/// 地址敏感的 task 本来也不该可移动。
	///
	/// @pre 本 entity 尚未登记过 `TaskType`。
	template <class TaskType, class... Args>
	auto emplace(Args&&... arguments) -> TaskType&
	{
		// `::entt::registry::context::emplace` 走的是 `try_emplace`：键已存在时**静默**
		// 返回旧对象，不替换也不报错。所以重复登记这条得我们自己拦。
		assert(!_tasks->contains<TaskType>() && "job arch: 同一个 task 类型重复 emplace");
		return _tasks->emplace<TaskType>(::std::forward<Args>(arguments)...);
	}
};

/// entity 要满足的形状：自己把全部 task 登记进 builder。
template <class EntityType, class ContextType>
concept graph_entity = requires (EntityType& object, ContextType& context, task_builder builder)
{
	{ object.build_task(context, builder) };
};

/// 本帧参与的 entity 集合。
///
/// entity 对象由调用者长期持有，这里只存引用；每帧新建的只有那层壳（task 池 + 标志）。
/// `ContextType` 是**使用者自己的**帧上下文，库不看它一眼，只负责转发给 `build_task`。
template <class ContextType>
struct entities
{
	/// 一个 entity 在本帧的壳。
	struct slot
	{
		void* _object = nullptr;
		::entt::id_type _type = 0;
		void (*_build_task)(void*, ContextType&, task_builder) = nullptr;
		::entt::registry::context _tasks{::std::allocator<void>{}};
		bool _is_built = false;
		bool _is_building = false;
	};

	/// `entity<T>()` 返回的东西。伪代码里那个 `entity` 壳，改名避开成员函数同名。
	struct handle
	{
		slot* _slot;
		ContextType* _context;

		[[nodiscard]] auto task_built() const noexcept -> bool
		{
			return _slot->_is_built;
		}

		/// 显式构建。
		/// @pre 尚未构建。想要"构建过就算了"的语义请直接用 `task<T>()`。
		auto build_task() const -> built_entity
		{
			assert(!_slot->_is_built && "job arch: entity 重复构建");
			return entities::_build(*_slot, *_context);
		}

		/// 取这个 entity 的 `TaskType`；**尚未构建就隐式构建**。没登记过返回 `nullptr`。
		template <class TaskType>
		[[nodiscard]] auto task() const -> TaskType*
		{
			if (!_slot->_is_built)
			{
				entities::_build(*_slot, *_context);
			}
			return _slot->_tasks.template find<TaskType>();
		}
	};

	ContextType* _context;
	// `unique_ptr`：构建是**重入**的（yyy 的构建里会去构建 xxx），此时外层正攥着
	// 指向某个 slot 的 handle。存值的话一次扩容就把它悬掉了。
	::std::vector<::std::unique_ptr<slot>> _slots;

	/// 上下文通常是把 `entities` 作为成员持有的，构造时传 `*this` 即可——
	/// 这里只记地址，不碰它，所以此刻 `ContextType` 还没构造完也没关系。
	explicit entities(ContextType& context) noexcept
		: _context(&context)
	{
	}

	entities(entities&&) = delete;
	auto operator=(entities&&) -> entities& = delete;

	~entities()
	{
		// 逆注册序销毁：后构建的 task 可能引用先构建的。
		while (!_slots.empty())
		{
			_slots.pop_back();
		}
	}

	/// 让这个 entity 参与本帧。不 `add` 就等于本帧不参与，别人 `entity<T>()` 查不到。
	/// @pre 本帧尚未注册过同类型的 entity。
	template <class EntityType>
		requires graph_entity<EntityType, ContextType>
	auto add(EntityType& object) -> void
	{
		auto const type = ::entt::type_hash<EntityType>::value();
		assert(_find(type) == nullptr && "job arch: 同一个 entity 类型重复注册");

		auto made = ::std::make_unique<slot>();
		made->_object = &object;
		made->_type = type;
		made->_build_task = [](void* target, ContextType& context, task_builder builder)
		{
			static_cast<EntityType*>(target)->build_task(context, builder);
		};
		_slots.push_back(::std::move(made));
	}

	/// 本帧有没有这个 entity。没有就是它这帧不参与——调用方自己决定怎么降级。
	template <class EntityType>
	[[nodiscard]] auto entity() noexcept -> ::std::optional<handle>
	{
		auto* const found = _find(::entt::type_hash<EntityType>::value());
		if (found == nullptr)
		{
			return ::std::nullopt;
		}
		return handle{found, _context};
	}

	/// 把还没构建的 entity 都构建掉。顺序无所谓：依赖方会先把被依赖方拽起来。
	auto build_all() -> void
	{
		// 用下标而不是迭代器：`build_task` 理论上可以往里塞新 entity。
		for (auto index = ::std::size_t{0}; index != _slots.size(); ++index)
		{
			auto&& target = *_slots[index];
			if (!target._is_built)
			{
				_build(target, *_context);
			}
		}
	}

	[[nodiscard]] auto _find(::entt::id_type type) noexcept -> slot*
	{
		for (auto&& target : _slots)
		{
			if (target->_type == type)
			{
				return target.get();
			}
		}
		return nullptr;
	}

	static auto _build(slot& target, ContextType& context) -> built_entity
	{
		assert(!target._is_building && "job arch: entity 构建期依赖成环");

		target._is_building = true;
		try
		{
			target._build_task(target._object, context, task_builder{&target._tasks});
		}
		catch (...)
		{
			target._is_building = false;
			throw;
		}
		target._is_building = false;
		target._is_built = true;

		return built_entity{&target._tasks};
	}
};
