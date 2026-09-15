// 设计依赖 EnTT 的四条假设，逐条验。
#include <entt/entity/registry.hpp>
#include <entt/core/type_info.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

int failures = 0;

void check(bool ok, char const* what)
{
	std::printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok) { ++failures; }
}

// 不可移动、不可拷贝 —— task 里会捆 sender 表达式和自引用，必须能装得下
struct immovable
{
	int _value;
	immovable* _self;

	explicit immovable(int value) : _value(value), _self(this) {}
	immovable(immovable&&) = delete;
	auto operator=(immovable&&) -> immovable& = delete;
};

struct big { char _pad[4096]; int _value; };

int main()
{
	// 1. context 能独立持有（不需要一整个 registry）
	::entt::registry::context tasks{::std::allocator<void>{}};
	check(true, "1 registry::context 可独立构造（ctor 是 public）");

	// 2. emplace / find 的基本形状
	{
		auto&& made = tasks.emplace<immovable>(42);
		auto* found = tasks.find<immovable>();
		check(found == &made && found->_value == 42, "2 emplace<T>(args...) / find<T>()");
		check(tasks.find<big>() == nullptr, "2 没登记过的 find<T>() 返回 nullptr");
	}

	// 3. 不可移动的 T 装得下，且地址稳定：塞很多别的类型逼它 rehash
	{
		auto* before = tasks.find<immovable>();
		check(before->_self == before, "3 不可移动的 T 构造在原地");

		// 每个类型一个 key，塞够多逼 dense_map 扩容
		tasks.emplace_as<big>(1001u);
		tasks.emplace_as<big>(1002u);
		tasks.emplace_as<big>(1003u);
		tasks.emplace_as<big>(1004u);
		tasks.emplace_as<big>(1005u);
		tasks.emplace_as<big>(1006u);
		tasks.emplace_as<big>(1007u);
		tasks.emplace_as<big>(1008u);
		tasks.emplace_as<big>(1009u);
		tasks.emplace_as<big>(1010u);
		tasks.emplace_as<big>(1011u);
		tasks.emplace_as<big>(1012u);
		tasks.emplace_as<big>(1013u);
		tasks.emplace_as<big>(1014u);
		tasks.emplace_as<big>(1015u);
		tasks.emplace_as<big>(1016u);

		auto* after = tasks.find<immovable>();
		check(after == before, "3 rehash 之后地址不变（basic_any<0u> 一律走堆）");
		check(after->_self == after, "3 对象里的自引用依然有效");
	}

	// 4. 重复 emplace 的行为：try_emplace 不替换，所以库要自己断言
	{
		auto* first = tasks.find<immovable>();
		auto&& again = tasks.emplace<immovable>(99);
		check(&again == first && first->_value == 42,
			"4 重复 emplace<T> 静默返回旧对象 -> 必须自己 contains<T>() 断言");
		check(tasks.contains<immovable>(), "4 contains<T>() 可用");
	}

	// 5. 类型标签：给 entity 用
	{
		auto const a = ::entt::type_id<immovable>().hash();
		auto const b = ::entt::type_hash<immovable>::value();
		check(a == b, "5 type_id<T>().hash() == type_hash<T>::value()");
		check(a != ::entt::type_hash<big>::value(), "5 不同类型标签不同");
	}

	// 6. context 可移动吗？（entities 的 slot 要放进容器）
	{
		constexpr bool movable = ::std::is_move_constructible_v<::entt::registry::context>;
		std::printf("  %-52s %s\n", "6 registry::context 是否可移动构造", movable ? "是" : "否");
	}

	std::printf("%s\n", failures == 0 ? "全部通过" : "有失败");
	return failures == 0 ? 0 : 1;
}
