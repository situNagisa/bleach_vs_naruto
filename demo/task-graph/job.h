#pragma once

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace bvn::task_graph
{
template <class Job>
concept graph_job = requires (Job& value)
{
	{ value.build() };
};

struct job_base
{
	constexpr virtual ~job_base() = default;
	virtual auto build() -> void = 0;
};

namespace details
{
template <graph_job Job>
struct erase_job final : job_base
{
	Job _value;

	template <class... Arguments>
	constexpr explicit erase_job(::std::in_place_t, Arguments&&... arguments)
		noexcept(::std::is_nothrow_constructible_v<Job, Arguments...>)
		: _value(::std::forward<Arguments>(arguments)...)
	{}

	constexpr auto build() noexcept(noexcept(_value.build())) -> void override
	{
		_value.build();
	}
};
}

/// 直接在擦除对象中构造，支持持有自身成员地址、不可移动的 job。
template <graph_job Job, class... Arguments>
	requires ::std::constructible_from<Job, Arguments...>
[[nodiscard]] constexpr auto erase_job(::std::in_place_type_t<Job>, Arguments&&... arguments)
	-> ::std::unique_ptr<job_base>
{
	return ::std::make_unique<details::erase_job<Job>>(
		::std::in_place, ::std::forward<Arguments>(arguments)...);
}

/// 持有传入 job 的副本或移动后的对象；后续 build() 转发到该对象。
template <class Job>
	requires graph_job<::std::remove_cvref_t<Job>>
		&& ::std::constructible_from<::std::remove_cvref_t<Job>, Job>
[[nodiscard]] constexpr auto erase_job(Job&& value) -> ::std::unique_ptr<job_base>
{
	return ::bvn::task_graph::erase_job(::std::in_place_type<::std::remove_cvref_t<Job>>,
		::std::forward<Job>(value));
}
}
