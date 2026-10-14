#pragma once

#include <array>
#include <functional>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include <nagisa/concurrency/lease.h>

#include "../framework/frame_context.h"
#include "../framework/frame_slot.h"
#include "../framework/any_sender.h"

#include "./detail/demo_vulkan.h"

struct renderer
{
	using resource_pool = ::nagisa::concurrency::bounded_lease_pool<frame_slot*, 4>;

	struct operation_state
	{
		using begin_sender_type = decltype(::exec::split(::std::declval<values_sender_type<resource_pool::lease>>()));
		using end_sender_type = decltype(::exec::split(::std::declval<values_sender_type<::std::reference_wrapper<frame_slot>>>()));
		using fence_sender_type = decltype(::exec::split(::std::declval<values_sender_type<>>()));

		renderer& _renderer;
		::std::vector<any_sender_type> recorders{};
		struct
		{
			::std::mutex mutex{};
			::std::vector<::vkkl::command_buffer_observer> commands{};
		} secondary{};
		any_scheduler_type scheduler{};

		begin_sender_type _begin;
		end_sender_type _end;
		fence_sender_type _fence;

		operation_state(renderer& r, any_scheduler_type sched);

		constexpr static auto _ref = ::stdexec::then([](resource_pool::lease const& frame) noexcept {return ::std::ref(*frame.token()); });

		struct shared_state
		{
			::std::reference_wrapper<operation_state> _state;

			[[nodiscard]] auto begin() const
			{
				return _state.get()._begin | _ref;
			}
			[[nodiscard]] auto end() const noexcept { return _state.get()._end; }
			[[nodiscard]] auto fence() const noexcept { return _state.get()._fence; }

			[[nodiscard]] auto&& recorders() const noexcept { return _state.get().recorders; }
			[[nodiscard]] auto&& secondary() const noexcept { return _state.get().secondary; }
			[[nodiscard]] auto scheduler() const noexcept { return _state.get().scheduler; }
		};

		shared_state start(frame_context& context) & noexcept;
	};

	renderer();

	void set_fullscreen(bool enabled);
	operation_state connect(any_scheduler_type sched) { return operation_state(*this, sched); }

	::bvn::platform::window window;
	::consumer_arch_vulkan::vulkan_context vulkan;
	::std::array<frame_slot, 4> _frames;
	resource_pool _pool;
};
