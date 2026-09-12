// when_all 到底有没有装那个 stop 回调？孩子看到的是外层 token 还是内部 source 的？
#include <stdexec/execution.hpp>

#include <atomic>
#include <cstdio>
#include <exception>
#include <typeinfo>

namespace ex = stdexec;

struct trigger
{
	std::atomic<void*> object{nullptr};
	void (*fire)(void*) noexcept = nullptr;
};

ex::inplace_stop_token g_child_token{};
bool g_child_token_is_inplace = false;

struct manual_sender
{
	using sender_concept = ex::sender_t;
	using completion_signatures = ex::completion_signatures<
		ex::set_value_t(), ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>;

	trigger* t;

	template <class Receiver>
	struct operation
	{
		using operation_state_concept = ex::operation_state_t;
		Receiver rcvr;
		trigger* t;

		void start() & noexcept
		{
			using token_type = ex::stop_token_of_t<ex::env_of_t<Receiver>>;
			std::printf("孩子看到的 token 类型 : %s\n", typeid(token_type).name());
			if constexpr (std::same_as<token_type, ex::inplace_stop_token>)
			{
				g_child_token_is_inplace = true;
				g_child_token = ex::get_stop_token(ex::get_env(rcvr));
			}
			t->fire = [](void* p) noexcept { ex::set_value(std::move(static_cast<operation*>(p)->rcvr)); };
			t->object.store(this, std::memory_order_release);
		}
	};

	template <ex::receiver Receiver>
	auto connect(Receiver r) && -> operation<Receiver> { return operation<Receiver>{std::move(r), t}; }
};

struct probe_receiver
{
	using receiver_concept = ex::receiver_t;
	char const** result;
	ex::inplace_stop_token token;
	void set_value() noexcept { *result = "set_value"; }
	void set_error(std::exception_ptr) noexcept { *result = "set_error"; }
	void set_stopped() noexcept { *result = "set_stopped"; }
	auto get_env() const noexcept { return ex::prop{ex::get_stop_token, token}; }
};

int main()
{
	trigger t;
	ex::inplace_stop_source source;
	char const* result = "(未完成)";

	auto op = ex::connect(ex::when_all(manual_sender{&t}, ex::just()),
		probe_receiver{&result, source.get_token()});
	ex::start(op);

	std::printf("外层 token 和孩子 token 是同一个吗 : %s\n",
		(g_child_token_is_inplace && g_child_token == source.get_token()) ? "是（没装内部 source）" : "否");
	std::printf("request_stop 之前，孩子 token stopped? %d\n", (int) g_child_token.stop_requested());

	source.request_stop();

	std::printf("request_stop 之后，孩子 token stopped? %d   <- 1 说明回调装上了且转发生效\n",
		(int) g_child_token.stop_requested());

	t.fire(t.object.load(std::memory_order_acquire));
	std::printf("最终完成通道 : %s\n", result);
	return 0;
}
