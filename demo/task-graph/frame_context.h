#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <typeinfo>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "job.h"
#include "node_sender.h"

namespace bvn::task_graph
{
struct frame_context
{
	// 两个令牌源都必须活到 job 和根节点销毁之后。
	::stdexec::inplace_stop_source _stop_source;
	// 结构边永不取消；可取消节点自行注入 stop_token()。
	::stdexec::inplace_stop_source _structural_source;
	::std::vector<::std::unique_ptr<job_base>> _jobs;
	::std::vector<node_sender> _roots;

	frame_context() = default;
	frame_context(frame_context const&) = delete;
	auto operator=(frame_context const&) -> frame_context& = delete;

	~frame_context()
	{
		_roots.clear();
		// 逆注册顺序销毁，保持 job 引用的构造期依赖存活。
		while (!_jobs.empty())
		{
			_jobs.pop_back();
		}
	}

	/// 接收 erase_job(value) 返回的所有权。
	/// @pre erased 非空，且本帧尚未注册相同类型的 job。
	auto add_job(::std::unique_ptr<job_base> erased) -> void
	{
		assert(erased && "task graph: null job");
		assert(::std::ranges::none_of(_jobs, [incoming = erased.get()](auto&& existing)
			{
				auto stored = existing.get();
				return typeid(*stored) == typeid(*incoming);
			}) && "task graph: duplicate job type");
		_jobs.push_back(::std::move(erased));
	}

	/// 原地构造不可移动的 job，然后交给同一个所有权入口。
	/// @pre 本帧尚未注册 Job。
	template <graph_job Job, class... Arguments>
	auto add_job(Arguments&&... arguments) -> Job&
	{
		assert(!job<Job>().has_value() && "task graph: duplicate job type");

		auto erased = ::bvn::task_graph::erase_job(::std::in_place_type<Job>, ::std::forward<Arguments>(arguments)...);
		auto&& value = static_cast<details::erase_job<Job>&>(*erased)._value;
		add_job(::std::move(erased));
		return value;
	}

	/// 查找本帧 Job 对象；缺失时返回 nullopt，引用随上下文失效。
	// C++23 的 optional 不支持引用；C++26 增加 optional<T&> 偏特化。
	// 标准条文：[optional.optional.ref] https://eel.is/c++draft/optional.optional.ref
	// 保持 C++23 兼容，暂用 optional<reference_wrapper<T>>；引用随上下文失效。
	template <graph_job Job>
	[[nodiscard]] auto job() noexcept -> ::std::optional<::std::reference_wrapper<Job>>
	{
		for (auto&& erased : _jobs)
		{
			if (auto value = dynamic_cast<details::erase_job<Job>*>(erased.get()))
			{
				return ::std::ref(value->_value);
			}
		}
		return ::std::nullopt;
	}

	/// 从只读上下文获取托管对象的可选只读引用。
	template <graph_job Job>
	[[nodiscard]] auto job() const noexcept -> ::std::optional<::std::reference_wrapper<Job const>>
	{
		for (auto&& erased : _jobs)
		{
			if (auto value = dynamic_cast<details::erase_job<Job> const*>(erased.get()))
			{
				return ::std::cref(value->_value);
			}
		}
		return ::std::nullopt;
	}

	auto add(node_sender node) -> void
	{
		_roots.push_back(::std::move(node));
	}

	[[nodiscard]] auto stop_token() const noexcept -> ::stdexec::inplace_stop_token
	{
		return _stop_source.get_token();
	}
};
}
