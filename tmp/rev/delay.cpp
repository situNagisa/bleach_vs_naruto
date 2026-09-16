// 怎么把"基类类型靠工厂推导"这件事推迟到 entity 闭合之后？
//
// 变体 3：task 是模板，基类依赖它自己的模板形参 —— 一定安全，但每个 entity 都要写一遍
//         `template <class Self> ... using nodes = nodes_template<entity>;`，而且要 this->_data。
// 变体 7：task 是**别名**（别名不实例化），`task_data` 直接**继承工厂的返回类型** ——
//         于是名字由 entity 自己的结果结构体提供，CTAD 推导成员类型。
#include <cstdio>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

// ---------------------------------------------------------------- 变体 3

template <class Factory, class... Args>
struct member_task_data
{
	::std::invoke_result_t<Factory, Args...> _data;

	explicit member_task_data(Args... arguments)
		: _data(Factory{}(::std::forward<Args>(arguments)...))
	{
	}
};

struct entity_self
{
	struct make_nodes
	{
		auto operator()(int seed) const
		{
			return ::std::tuple{::std::to_string(seed), seed * 2};
		}
	};

	template <class Self>
	struct nodes_template : member_task_data<typename Self::make_nodes, int>
	{
		using base_type = member_task_data<typename Self::make_nodes, int>;
		using base_type::base_type;

		[[nodiscard]] auto text() -> auto& { return ::std::get<0>(this->_data); }
		[[nodiscard]] auto doubled() -> auto& { return ::std::get<1>(this->_data); }
	};

	using nodes = nodes_template<entity_self>;
};

// ---------------------------------------------------------------- 变体 7

/// 继承工厂的返回类型：名字、方法、几个成员，全由那个返回类型说了算。
template <class Factory, class... Args>
struct task_data : ::std::invoke_result_t<Factory, Args...>
{
	using data_type = ::std::invoke_result_t<Factory, Args...>;

	explicit task_data(Args... arguments)
		: data_type(Factory{}(::std::forward<Args>(arguments)...))
	{
	}

	task_data(task_data&&) = delete;
	auto operator=(task_data&&) -> task_data& = delete;
};

/// 带状态：状态那层基类先构造好，再交给工厂。
template <class Factory, class StateType, class... Args>
struct task_data_with
	: StateType
	, ::std::invoke_result_t<Factory, StateType&, Args...>
{
	using data_type = ::std::invoke_result_t<Factory, StateType&, Args...>;

	explicit task_data_with(Args... arguments)
		: data_type(Factory{}(static_cast<StateType&>(*this), ::std::forward<Args>(arguments)...))
	{
	}

	task_data_with(task_data_with&&) = delete;
	auto operator=(task_data_with&&) -> task_data_with& = delete;
};

struct entity_inherit
{
	/// 名字住在这里。成员类型 CTAD 推出来，一遍都不用写。
	template <class TextType, class NumberType>
	struct node_set
	{
		TextType _text;
		NumberType _number;

		[[nodiscard]] auto text() noexcept -> auto& { return _text; }
		[[nodiscard]] auto doubled() noexcept -> auto& { return _number; }
	};
	template <class T, class N>
	node_set(T, N) -> node_set<T, N>;

	struct counter
	{
		int _calls = 0;
		auto bump() noexcept -> void { ++_calls; }
	};

	struct make_nodes
	{
		auto operator()(counter& state, int seed) const
		{
			state.bump();
			return node_set{::std::to_string(seed), seed * 2};
		}
	};

	using nodes = task_data_with<make_nodes, counter, int>;
};

struct entity_stateless
{
	template <class TextType>
	struct node_set
	{
		TextType _text;
		[[nodiscard]] auto text() noexcept -> auto& { return _text; }
	};
	template <class T>
	node_set(T) -> node_set<T>;

	struct make_nodes
	{
		auto operator()(int seed) const { return node_set{::std::to_string(seed)}; }
	};

	using nodes = task_data<make_nodes, int>;
};

int main()
{
	auto a = entity_self::nodes{7};
	::std::printf("变体 3（Self 形参）    : %s / %d\n", a.text().c_str(), a.doubled());

	auto b = entity_inherit::nodes{8};
	::std::printf("变体 7（继承返回类型） : %s / %d  工厂被调 %d 次\n",
		b.text().c_str(), b.doubled(), b._calls);

	auto c = entity_stateless::nodes{9};
	::std::printf("变体 7（无状态）       : %s\n", c.text().c_str());
	return 0;
}
