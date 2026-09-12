// 扫描：让取消线程在开火前自旋 N 次，找出能命中 when_all 双重完成的偏移。
#include <stdexec/execution.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <thread>

namespace ex = stdexec;

struct trigger
{
	std::atomic<void*> object{nullptr};
	void (*fire)(void*) noexcept = nullptr;
};

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
			t->fire = [](void* p) noexcept { ex::set_value(std::move(static_cast<operation*>(p)->rcvr)); };
			t->object.store(this, std::memory_order_release);
		}
	};

	template <ex::receiver Receiver>
	auto connect(Receiver r) && -> operation<Receiver> { return operation<Receiver>{std::move(r), t}; }
};

std::atomic<int> g_value{0};
std::atomic<int> g_error{0};
std::atomic<int> g_stopped{0};

struct counting_receiver
{
	using receiver_concept = ex::receiver_t;
	std::atomic<int>* completions;
	ex::inplace_stop_token token;
	void set_value() noexcept { g_value.fetch_add(1); completions->fetch_add(1); }
	void set_error(std::exception_ptr) noexcept { g_error.fetch_add(1); completions->fetch_add(1); }
	void set_stopped() noexcept { g_stopped.fetch_add(1); completions->fetch_add(1); }
	auto get_env() const noexcept { return ex::prop{ex::get_stop_token, token}; }
};

[[gnu::noinline]] void spin(int n)
{
	for (int i = 0; i < n; ++i) { __builtin_ia32_pause(); }
}

// 返回命中的轮次；0 表示没命中
int run(int delay, int rounds)
{
	for (int round = 1; round <= rounds; ++round)
	{
		trigger t;
		ex::inplace_stop_source source;
		std::atomic<int> completions{0};

		auto op = ex::connect(ex::when_all(manual_sender{&t}, ex::just()),
			counting_receiver{&completions, source.get_token()});
		ex::start(op);

		std::atomic<int> ready{0};
		std::atomic<bool> go{false};

		std::thread a{[&] {
			ready.fetch_add(1, std::memory_order_release);
			while (!go.load(std::memory_order_acquire)) { }
			t.fire(t.object.load(std::memory_order_acquire));
		}};
		std::thread b{[&] {
			ready.fetch_add(1, std::memory_order_release);
			while (!go.load(std::memory_order_acquire)) { }
			spin(delay);
			source.request_stop();
		}};

		while (ready.load(std::memory_order_acquire) < 2) { }
		go.store(true, std::memory_order_release);
		a.join();
		b.join();

		if (completions.load(std::memory_order_relaxed) != 1)
		{
			std::printf("  delay=%-5d 第 %d 轮命中：receiver 被完成 %d 次\n",
				delay, round, completions.load(std::memory_order_relaxed));
			std::fflush(stdout);
			std::_Exit(1);
		}
	}
	return 0;
}

int main(int argc, char** argv)
{
	int const rounds = argc > 1 ? std::atoi(argv[1]) : 3000;
	std::printf("stdexec::when_all 双重完成扫描（每档 %d 轮）\n", rounds);
	for (int delay : {0, 2, 5, 10, 20, 40, 70, 100, 150, 200, 300, 500, 800, 1200, 2000})
	{
		g_value = 0; g_error = 0; g_stopped = 0;
		run(delay, rounds);
		std::printf("  delay=%-5d 未命中  (value=%d stopped=%d error=%d)\n",
			delay, g_value.load(), g_stopped.load(), g_error.load());
		std::fflush(stdout);
	}
	std::printf("全部未命中\n");
	return 0;
}
