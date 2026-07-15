#pragma once

#include <array>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include <exec/async_scope.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <bvn/graphics/secondary_command_buffer.h>
#include <bvn/graphics/vulkan_renderer.h>
#include <bvn/graphics/environment.h>

NAGISA_BUILD_LIB_DETAIL_BEGIN

struct render_frame_completion_callback
{
	render_frame_completion_callback* _next = nullptr;
	void* _object = nullptr;
	void (*_complete)(void*) noexcept = nullptr;
};

struct render_frame_state
{
	struct secondary_command
	{
		VkCommandBuffer _handle = VK_NULL_HANDLE;
		bool _published = false;
	};

	::std::mutex _mutex;
	::std::vector<secondary_command> _secondary_commands;
	render_frame_completion_callback* _callbacks = nullptr;
	bool _completed = false;

	[[nodiscard]] auto publish(::std::size_t index, render_frame_completion_callback& callback) noexcept -> bool;
	auto complete() noexcept -> void;
};

NAGISA_BUILD_LIB_DETAIL_END

NAGISA_BUILD_LIB_BEGIN

/// A task-owned command lease reserved in one frame generation but not yet published.
struct secondary_command_recording
{
	::std::shared_ptr<details::render_frame_state> _state;
	::std::size_t _generation_index = 0;
	secondary_command_pool::command_buffer _command;

	secondary_command_recording(
		::std::shared_ptr<details::render_frame_state> state,
		::std::size_t generation_index,
		secondary_command_pool::command_buffer command
	) noexcept;

	secondary_command_recording(secondary_command_recording const&) = delete;
	auto operator=(secondary_command_recording const&) -> secondary_command_recording& = delete;
	secondary_command_recording(secondary_command_recording&&) noexcept = default;
	auto operator=(secondary_command_recording&&) -> secondary_command_recording& = delete;

	[[nodiscard]] constexpr auto get() const noexcept -> VkCommandBuffer
	{
		return _command.get();
	}
};

/// A value-only sender that recycles one command buffer after its exact frame completes.
struct secondary_command_retirement
{
	using sender_concept = ::stdexec::sender_tag;
	using completion_signatures = ::stdexec::completion_signatures<::stdexec::set_value_t()>;

	template <class Receiver>
	struct operation
	{
		secondary_command_recording _recording;
		Receiver _receiver;
		details::render_frame_completion_callback _callback;

		operation(
			secondary_command_recording recording,
			Receiver receiver
		) noexcept(::std::is_nothrow_move_constructible_v<Receiver>)
			: _recording(::std::move(recording))
			, _receiver(::std::move(receiver))
			, _callback
			{
				._object = this,
				._complete = [](void* object) noexcept
				{
					auto self = static_cast<operation*>(object);
					self->_recording._command.recycle();
					::stdexec::set_value(::std::move(self->_receiver));
				},
			}
		{}

		operation(operation const&) = delete;
		auto operator=(operation const&) -> operation& = delete;
		operation(operation&&) = delete;
		auto operator=(operation&&) -> operation& = delete;

		auto start() & noexcept -> void
		{
			if (!_recording._state->publish(_recording._generation_index, _callback))
			{
				_callback._complete(_callback._object);
			}
		}
	};

	secondary_command_recording _recording;

	explicit secondary_command_retirement(secondary_command_recording recording) noexcept;

	secondary_command_retirement(secondary_command_retirement const&) = delete;
	auto operator=(secondary_command_retirement const&) -> secondary_command_retirement& = delete;
	secondary_command_retirement(secondary_command_retirement&&) noexcept = default;
	auto operator=(secondary_command_retirement&&) -> secondary_command_retirement& = delete;

	template <class Receiver>
	[[nodiscard]] auto connect(Receiver receiver) && -> operation<Receiver>
	{
		return operation<Receiver>{::std::move(_recording), ::std::move(receiver)};
	}
};

/// The renderer observer and command-allocation context for one recording generation.
struct render_recording_frame : frame_vulkan_env_renderer
{
	::std::shared_ptr<details::render_frame_state> _state;
	secondary_command_pool* _commands = nullptr;

	constexpr render_recording_frame() noexcept = default;
	render_recording_frame(
		frame_vulkan_env_renderer renderer,
		::std::shared_ptr<details::render_frame_state> state,
		secondary_command_pool& commands
	) noexcept;

	[[nodiscard]] explicit operator bool() const noexcept;
	[[nodiscard]] auto allocate() const -> secondary_command_recording;
	[[nodiscard]] auto retire(secondary_command_recording command) const noexcept -> secondary_command_retirement;
};

static_assert(frame_env_renderer<render_recording_frame>);
static_assert(::std::is_nothrow_move_assignable_v<render_recording_frame>);
static_assert(::stdexec::sender<secondary_command_retirement>);

/// Coordinates frame recording, submission, presentation, and frame-generation completion.
struct render_workflow
{
	struct async_record_awaitable
	{
		render_workflow* _workflow = nullptr;
		secondary_command_pool* _commands = nullptr;
		::std::coroutine_handle<> _parent;
		render_recording_frame _frame;

		static constexpr auto await_ready() noexcept { return false; }
		auto await_suspend(::std::coroutine_handle<> parent) -> bool;
		auto await_resume() noexcept -> render_recording_frame;
	};

	vulkan_context& renderer;
	::exec::static_thread_pool _inner_pool{1};
	::exec::static_thread_pool _recording_pool{};
	::exec::async_scope _inner_scope{};
	::std::mutex _waiters_mutex;
	::std::mutex _submit_error_mutex;
	::stdexec::inplace_stop_source _stop_source;
	::std::vector<async_record_awaitable*> _waiters;
	::std::array<::std::shared_ptr<details::render_frame_state>, frames_in_flight> _submitted_frames;
	::std::exception_ptr _submit_error;
	::std::uint32_t _current_slot = 0;
	::std::uint32_t _active_image_index = 0;

	explicit render_workflow(vulkan_context& renderer) noexcept;

	/// Returns a scheduler value without lending ownership of the internal pool.
	[[nodiscard]] auto get_scheduler() noexcept -> ::exec::static_thread_pool::scheduler;
	/// Waits until the current CPU-side submit work and its scheduler queue are drained.
	auto wait_for_submit() -> void;
	/// Requests the stop-only submit path used to resume parked render tasks.
	auto request_stop() noexcept -> void;
	/// Stops the internal pools after all render tasks and submit work are drained.
	auto finish() noexcept -> void;

	[[nodiscard]] constexpr auto async_record(secondary_command_pool& commands) noexcept
	{
		return async_record_awaitable
		{
			._workflow = this,
			._commands = &commands,
			._parent = {},
			._frame = {},
		};
	}

	auto submit() -> void;

	/// Completes every submitted generation after device idle or device loss ends GPU access.
	auto complete_submissions() noexcept -> void;
};

NAGISA_BUILD_LIB_END

#include <nagisa/build_lib/destruct.h>
