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
///
/// 按职责分成三样，能力靠**类型**隔开，不靠约定：
///
/// | 谁 | 拿到什么 | 能干什么 |
/// | --- | --- | --- |
/// | entity 的 `build_task` | `entity_view` | 只能 `entity<T>()` 查别人 |
/// | 调用者（main） | `entity_storage` | `add` / `find` —— 容器原语 |
/// | 调用者（main） | `build_all` 自由函数 | 驱动一轮构建 |
///
/// 于是 entity 手里**根本没有**通往 `add` 的路径：`entity_view` 上不存在这个名字。
/// `build_all` 是自由函数而不是成员，因为"驱动一轮构建"跟"存 entity"是两件事。


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

/// entity 容器。**只有容器原语**：增、查、遍历。驱动构建是 `build_all` 的事。
///
/// `view` 和 `slot` 嵌在里面，是为了解开声明环：`view` 要提 `entity_storage`、
/// `slot` 的跳板签名要提 `view`。嵌套之后 `view` 先声明、`slot` 后声明，
/// 两者都能用注入类名提到外层，一个前向声明都不用写。
template <class ContextType>
struct entity_storage
{
	/// **entity 在 `build_task` 里拿到的东西。只有查这一个能力。**
	struct view
	{
		entity_storage* _entities = nullptr;
		ContextType* _context = nullptr;

		/// 本帧有没有这个 entity。没有就是它这帧不参与——调用方自己决定怎么降级。
		/// 返回类型推导 + 类外定义：用到的时候 `entity_handle` 已经完整了。
		template <class EntityType>
		[[nodiscard]] auto entity() const;
	};

	/// 一个 entity 在本帧的壳：task 池 + 构建标志。entity 对象本身由调用者长期持有。
	struct slot
	{
		void* _object = nullptr;
		::entt::id_type _type = 0;
		void (*_build_task)(void*, view, task_builder) = nullptr;
		::entt::registry::context _tasks{::std::allocator<void>{}};
		bool _is_built = false;
		bool _is_building = false;
	};

	// `unique_ptr`：构建是**重入**的（yyy 的构建里会去构建 xxx），此时外层正攥着
	// 指向某个 slot 的 handle。存值的话一次扩容就把它悬掉了。
	::std::vector<::std::unique_ptr<slot>> _slots;

	entity_storage() = default;
	entity_storage(entity_storage&&) = delete;
	auto operator=(entity_storage&&) -> entity_storage& = delete;

	~entity_storage()
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
	auto add(EntityType& object) -> void
	{
		// 约束写在体内而不是 requires 子句上：具名 concept 要提 `view`，而 `view` 是
		// 本类的嵌套类型，在类外才能给它起名字。写成 static_assert 报错一样清楚。
		static_assert(
			requires (EntityType& target, ContextType& context, view seen, task_builder builder)
			{
				{ target.build_task(context, seen, builder) };
			},
			"job arch: entity 必须提供 build_task(ContextType&, view, task_builder)");

		auto const type = ::entt::type_hash<EntityType>::value();
		assert(find(type) == nullptr && "job arch: 同一个 entity 类型重复注册");

		auto made = ::std::make_unique<slot>();
		made->_object = &object;
		made->_type = type;
		made->_build_task = [](void* target, view seen, task_builder builder)
		{
			static_cast<EntityType*>(target)->build_task(*seen._context, seen, builder);
		};
		_slots.push_back(::std::move(made));
	}

	[[nodiscard]] auto find(::entt::id_type type) noexcept -> slot*
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

	[[nodiscard]] auto size() const noexcept -> ::std::size_t { return _slots.size(); }

	[[nodiscard]] auto operator[](::std::size_t index) noexcept -> slot& { return *_slots[index]; }
};

/// `build_task` 的参数类型。写成别名，签名里就不用拖着 `typename ...::view`。
template <class ContextType>
using entity_view = typename entity_storage<ContextType>::view;

/// 构建一个 entity：调它的 `build_task`，把全部 task 登记进去。
/// @pre 不在构建中——递归撞回来就是环。
template <class ContextType>
auto build_entity(typename entity_storage<ContextType>::slot& target, entity_view<ContextType> seen)
	-> built_entity
{
	assert(!target._is_building && "job arch: entity 构建期依赖成环");

	target._is_building = true;
	try
	{
		target._build_task(target._object, seen, task_builder{&target._tasks});
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

/// `entity_view::entity<T>()` 查到的东西。伪代码里那个 `entity` 壳。
template <class ContextType>
struct entity_handle
{
	typename entity_storage<ContextType>::slot* _slot;
	entity_view<ContextType> _view;

	[[nodiscard]] auto task_built() const noexcept -> bool
	{
		return _slot->_is_built;
	}

	/// 显式构建。
	/// @pre 尚未构建。想要"构建过就算了"的语义请直接用 `task<T>()`。
	auto build_task() const -> built_entity
	{
		assert(!_slot->_is_built && "job arch: entity 重复构建");
		return build_entity<ContextType>(*_slot, _view);
	}

	/// 取这个 entity 的 `TaskType`；**尚未构建就隐式构建**。没登记过返回 `nullptr`。
	template <class TaskType>
	[[nodiscard]] auto task() const -> TaskType*
	{
		if (!_slot->_is_built)
		{
			build_entity<ContextType>(*_slot, _view);
		}
		return _slot->_tasks.template find<TaskType>();
	}
};

template <class ContextType>
template <class EntityType>
auto entity_storage<ContextType>::view::entity() const
{
	using handle_type = entity_handle<ContextType>;

	auto* const found = _entities->find(::entt::type_hash<EntityType>::value());
	if (found == nullptr)
	{
		return ::std::optional<handle_type>{};
	}
	return ::std::optional<handle_type>{handle_type{found, *this}};
}

/// 把容器里还没构建的 entity 都构建掉。顺序无所谓：依赖方会先把被依赖方拽起来。
///
/// 自由函数而不是成员：存 entity 是容器的事，驱动一轮构建是另一层语义。
template <class ContextType>
auto build_all(entity_storage<ContextType>& entities, ContextType& context) -> void
{
	auto const seen = entity_view<ContextType>{&entities, &context};

	// 用下标而不是迭代器：`build_task` 理论上可以往容器里塞新 entity。
	for (auto index = ::std::size_t{0}; index != entities.size(); ++index)
	{
		auto&& target = entities[index];
		if (!target._is_built)
		{
			build_entity<ContextType>(target, seen);
		}
	}
}
