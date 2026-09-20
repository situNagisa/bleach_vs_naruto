#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <utility>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_video.h>
#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include "../framework/frame_context.h"
#include "../framework/entities.h"

/// 单窗口输入 entity。每帧首次启动 poll() 必须在 SDL 主线程；之后可跨线程订阅。
struct input
{
	/// pressed/released 是本帧边沿；同一帧内按下再松开时，两者都为 true。
	struct button_state
	{
		bool down = false;
		bool pressed = false;
		bool released = false;

		constexpr auto _update(bool value) noexcept -> void
		{
			pressed |= value && !down;
			released |= !value && down;
			down = value;
		}

		constexpr auto _reset_edges() noexcept -> void
		{
			pressed = false;
			released = false;
		}
	};

	struct keyboard_state
	{
		/// 按 SDL 物理扫描码查询；down 持续到松开，自动重复不产生新的 pressed。
		[[nodiscard]] constexpr auto key(::SDL_Scancode code) const noexcept -> button_state const&
		{
			assert(code > ::SDL_SCANCODE_UNKNOWN && code < ::SDL_SCANCODE_COUNT);
			return _keys[static_cast<::std::size_t>(code)];
		}

		::SDL_Keymod modifiers = SDL_KMOD_NONE;
		::std::array<button_state, ::SDL_SCANCODE_COUNT> _keys{};
	};

	struct mouse_state
	{
		/// 使用 SDL_BUTTON_LEFT/RIGHT/MIDDLE/X1/X2 等从 1 开始的按钮编号。
		[[nodiscard]] constexpr auto button(::Uint8 index) const noexcept -> button_state const&
		{
			assert(index > 0 && index <= _buttons.size());
			return _buttons[index - 1];
		}

		::SDL_FPoint position{}; ///< 窗口坐标，与 SDL 鼠标事件坐标一致。
		::SDL_FPoint delta{}; ///< 本帧累计相对位移。
		::SDL_FPoint wheel{}; ///< 本帧累计滚轮量，已归一化 SDL_MOUSEWHEEL_FLIPPED。
		::std::array<button_state, 32> _buttons{};
	};

	/// entity 持有的跨帧状态；poll() 按值复制到 split 的完成值中作为快照。
	struct state
	{
		keyboard_state keyboard{};
		mouse_state mouse{};
		bool quit_requested = false;
	};

	template <class Poll>
	struct _senders
	{
		Poll _poll;

		/// split 持有完整 state 快照，以 state const& 交给各个订阅者。
		[[nodiscard]] auto&& poll() const noexcept { return _poll; }
	};

	template <class Poll>
	_senders(Poll) -> _senders<Poll>;

	struct _task_fn
	{
		auto operator()(input& source) const
		{
			auto poll = ::stdexec::just()
				| ::stdexec::then([&source]
					{
						return source._poll();
					})
				| ::exec::split();
			return _senders{::std::move(poll)};
		}
	};

	using task = task_data<_task_fn, input&>;

	/// 借用窗口的 ID；窗口和 SDL 初始化必须覆盖 input 的使用期。
	explicit input(::SDL_Window& window)
		: _window_id(::SDL_GetWindowID(&window))
	{
		assert(::SDL_IsMainThread());
		assert(_window_id != 0);
		if (::SDL_GetKeyboardFocus() == &window)
		{
			_sync_keyboard();
		}
		if (::SDL_GetMouseFocus() == &window)
		{
			auto const buttons = ::SDL_GetMouseState(&_current.mouse.position.x, &_current.mouse.position.y);
			for (auto index = ::std::size_t{}; index < _current.mouse._buttons.size(); ++index)
			{
				_current.mouse._buttons[index].down = (buttons & (::SDL_MouseButtonFlags{1} << index)) != 0;
			}
		}
	}

	/// 仅登记共享输入 task 和根节点；构建本身不采集事件或另行登记 state。
	auto build_task(frame_context& context, entity_view<frame_context>, task_builder builder) -> void
	{
		auto&& result = builder.emplace<task>(*this);
		context.roots.emplace_back(result.poll());
	}

	auto _poll() -> state
	{
		assert(::SDL_IsMainThread());
		for (auto&& key : _current.keyboard._keys)
		{
			key._reset_edges();
		}
		for (auto&& button : _current.mouse._buttons)
		{
			button._reset_edges();
		}
		_current.mouse.delta = {};
		_current.mouse.wheel = {};

		auto event = ::SDL_Event{};
		while (::SDL_PollEvent(&event))
		{
			_process_event(event);
		}
		// split 持有值副本，后续采集只更新 entity 内的跨帧状态。
		return _current;
	}

	auto _sync_keyboard() noexcept -> void
	{
		auto count = int{};
		auto const keys = ::SDL_GetKeyboardState(&count);
		for (auto index = ::std::size_t{}; index < _current.keyboard._keys.size(); ++index)
		{
			_current.keyboard._keys[index].down = index < static_cast<::std::size_t>(count) && keys[index];
		}
		_current.keyboard.modifiers = ::SDL_GetModState();
	}

	auto _process_event(::SDL_Event const& event) noexcept -> void
	{
		switch (event.type)
		{
		case ::SDL_EVENT_QUIT:
			_current.quit_requested = true;
			break;
		case ::SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			if (event.window.windowID == _window_id)
			{
				_current.quit_requested = true;
			}
			break;
		case ::SDL_EVENT_KEY_DOWN:
		case ::SDL_EVENT_KEY_UP:
			if (event.key.windowID == _window_id)
			{
				_current.keyboard.modifiers = event.key.mod;
				if (event.key.scancode > ::SDL_SCANCODE_UNKNOWN && event.key.scancode < ::SDL_SCANCODE_COUNT)
				{
					auto&& key = _current.keyboard._keys[static_cast<::std::size_t>(event.key.scancode)];
					if (event.key.repeat)
					{
						key.down = true;
					}
					else
					{
						key._update(event.type == ::SDL_EVENT_KEY_DOWN);
					}
				}
			}
			break;
		case ::SDL_EVENT_MOUSE_BUTTON_DOWN:
		case ::SDL_EVENT_MOUSE_BUTTON_UP:
			if (event.button.windowID == _window_id)
			{
				_current.mouse.position = {event.button.x, event.button.y};
				if (event.button.button > 0 && event.button.button <= _current.mouse._buttons.size())
				{
					_current.mouse._buttons[event.button.button - 1]._update(event.type == ::SDL_EVENT_MOUSE_BUTTON_DOWN);
				}
			}
			break;
		case ::SDL_EVENT_MOUSE_MOTION:
			if (event.motion.windowID == _window_id)
			{
				_current.mouse.position = {event.motion.x, event.motion.y};
				_current.mouse.delta.x += event.motion.xrel;
				_current.mouse.delta.y += event.motion.yrel;
			}
			break;
		case ::SDL_EVENT_MOUSE_WHEEL:
			if (event.wheel.windowID == _window_id)
			{
				auto const direction = event.wheel.direction == ::SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
				_current.mouse.wheel.x += direction * event.wheel.x;
				_current.mouse.wheel.y += direction * event.wheel.y;
			}
			break;
		case ::SDL_EVENT_WINDOW_FOCUS_LOST:
			if (event.window.windowID == _window_id)
			{
				// 失去焦点后，松开事件可能发往其他窗口，主动释放避免输入卡住。
				for (auto&& key : _current.keyboard._keys)
				{
					key._update(false);
				}
				_current.keyboard.modifiers = SDL_KMOD_NONE;
				for (auto&& button : _current.mouse._buttons)
				{
					button._update(false);
				}
			}
			break;
		default:
			break;
		}
	}

	::SDL_WindowID _window_id;
	state _current{};
};
