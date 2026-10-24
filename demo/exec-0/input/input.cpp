module;

// 只为 GLFW_KEY_* 常量；窗口本身通过 demo.window 访问。
#include <GLFW/glfw3.h>

module demo.input;

input::input(window const& target)
	: _window(::std::addressof(target))
{
}

values_sender_type<input::guard_type::permit, input::change_type> input::update()
{
	return {
		// 等价于 guard.acquire()（nagisa 的实现就是 _semaphore.acquire() | then(构造 permit)）。在本单元直接调用 guard.acquire() 时，
		// clang 22.1.8 不带 -g 编译（release 预设）会在实例化 basic_semaphore_guard<1>::acquire 时 SIGSEGV；带 -g 或展开写法都正常。
		guard._semaphore.acquire()
		| ::stdexec::then([this]() noexcept { return guard_type::permit{ guard._semaphore }; })
		| ::stdexec::let_value([this](guard_type::permit& permit)
			{
				window::poll_events();

				static constexpr auto keys = ::std::array{
					GLFW_KEY_ESCAPE,
					GLFW_KEY_UP, GLFW_KEY_DOWN,
					GLFW_KEY_LEFT, GLFW_KEY_RIGHT,
					GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L,
					GLFW_KEY_U, GLFW_KEY_I, GLFW_KEY_O,
					GLFW_KEY_P,
					GLFW_KEY_ENTER,
				};
				static_assert(keys.size() == key_count);

				// 失去焦点时视为所有键都已抬起；关闭请求不受焦点影响。
				auto next = state_type{ .close_requested = _window->should_close() };
				if (_window->focused())
				{
					for (auto const [index, key] : keys | ::std::views::enumerate)
						next.pressed[static_cast<::std::size_t>(index)] = _window->key_pressed(key);
					next.pressed[::std::to_underlying(key_code::confirm)] = _window->key_pressed(GLFW_KEY_ENTER)
						|| _window->key_pressed(GLFW_KEY_KP_ENTER) || _window->key_pressed(GLFW_KEY_SPACE);
				}

				auto change = change_type{ .close_requested = next.close_requested && !state.close_requested };
				for (auto const [index, now, before] : ::std::views::zip(::std::views::iota(::std::size_t{}), next.pressed, state.pressed))
					change.keys[index] = now == before ? key_change::none : now ? key_change::pressed : key_change::released;

				// 写入 state 后连同 permit 交出：调用方读完 state / 变化值后再决定何时释放。
				state = next;
				return ::stdexec::just(::std::move(permit), change);
			})
	};
}
