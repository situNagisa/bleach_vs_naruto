// 验证 P2：计数归零之后、回调注销之前，回调若被启动，会有第二个线程也判定自己是"最后一个"，
// 于是 receiver 被完成两次。
//
// 怎么测：
//   孩子换成"手动点火"的 sender，最后一次 arrive 的时刻完全由测试线程决定。
//   线程 A 点火（→ _arrive → 计数归零），线程 B 同时 request_stop（→ forward_stop），
//   两边用自旋栅栏对齐。receiver 只数自己被完成了几次。
//
// 两个可执行：
//   race_real   —— 用未经修改的 dynamic_when_all.h。completions >= 2 就是铁证，
//                  一命中就停：双重完成之后全是 UB，再统计没意义。
//   race_probe  —— 用 probe_when_all.h（只在 _arrive 里多两条原子指令记 claim 数，
//                  并拦住第二个 claimer 不让它真的完成，从而全程无 UB）。
//                  代价是那两条原子指令本身把窗口撑大了，所以它的数只能看趋势。
//                  widened 模式在窗口里插 200us 睡眠，用来证明这条路径可达。
//
// 构建/运行：tmp/rev/build10.sh
#ifdef USE_PROBE
#  include "probe_when_all.h"
#else
#  include "dynamic_when_all.h"
#  include <atomic>
// 真实头没有探针全局量，补几个哑元，让同一份 harness 两种模式都能编
inline ::std::atomic<int> probe_last_claims{0};
inline ::std::atomic<bool> probe_completed{false};
inline void (*probe_window_hook)() = nullptr;
#endif

#include <cstdio>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <exception>

namespace ex = ::stdexec;

// ---------- 手动点火的孩子 ----------
struct trigger
{
	::std::atomic<void*> _object{nullptr};
	void (*_fire)(void*) noexcept = nullptr;
};

struct manual_sender
{
	using sender_concept = ex::sender_t;
	using completion_signatures = ex::completion_signatures<
		ex::set_value_t(), ex::set_error_t(::std::exception_ptr), ex::set_stopped_t()>;

	trigger* _trigger;

	template <class ReceiverType>
	struct operation_type
	{
		using operation_state_concept = ex::operation_state_t;

		ReceiverType _receiver;
		trigger* _trigger;

		auto start() & noexcept -> void
		{
			_trigger->_fire = [](void* object) noexcept {
				ex::set_value(::std::move(static_cast<operation_type*>(object)->_receiver));
			};
			_trigger->_object.store(this, ::std::memory_order_release);
		}
	};

	template <ex::receiver ReceiverType>
	auto connect(ReceiverType receiver) && -> operation_type<ReceiverType>
	{
		return operation_type<ReceiverType>{::std::move(receiver), _trigger};
	}
};

// ---------- 只数完成次数的 receiver ----------
struct counting_receiver
{
	using receiver_concept = ex::receiver_t;

	::std::atomic<int>* _completions;
	ex::inplace_stop_token _token;

	auto set_value() noexcept -> void { _completions->fetch_add(1, ::std::memory_order_relaxed); }
	auto set_error(::std::exception_ptr) noexcept -> void { _completions->fetch_add(1, ::std::memory_order_relaxed); }
	auto set_stopped() noexcept -> void { _completions->fetch_add(1, ::std::memory_order_relaxed); }
	[[nodiscard]] auto get_env() const noexcept { return ex::prop{ex::get_stop_token, _token}; }
};

// ---------- 一轮 ----------
struct harness
{
	::std::atomic<int> _generation{0};
	::std::atomic<int> _ready{0};
	::std::atomic<int> _done{0};
	::std::atomic<bool> _quit{false};

	trigger _trigger{};
	ex::inplace_stop_source* _source = nullptr;

	auto _await(int generation) noexcept -> bool
	{
		_ready.fetch_add(1, ::std::memory_order_acq_rel);
		while (_generation.load(::std::memory_order_acquire) != generation)
		{
			if (_quit.load(::std::memory_order_relaxed)) { return false; }
		}
		return true;
	}
};

auto run_mode(char const* label, int rounds, void (*hook)(), bool stop_at_first_hit = false) -> int
{
	probe_window_hook = hook;

	harness bench;
	int doubles = 0;
	int claims = 0;
	auto const started = ::std::chrono::steady_clock::now();

	// 线程 A：点火（最后一个孩子完成）
	::std::thread thread_a{[&] {
		for (int round = 1; round <= rounds; ++round)
		{
			if (!bench._await(round)) { return; }
			auto* object = bench._trigger._object.load(::std::memory_order_acquire);
			bench._trigger._fire(object);
			bench._done.fetch_add(1, ::std::memory_order_release);
		}
	}};

	// 线程 B：外部取消
	::std::thread thread_b{[&] {
		for (int round = 1; round <= rounds; ++round)
		{
			if (!bench._await(round)) { return; }
			bench._source->request_stop();
			bench._done.fetch_add(1, ::std::memory_order_release);
		}
	}};

	for (int round = 1; round <= rounds; ++round)
	{
		ex::inplace_stop_source source;
		::std::atomic<int> completions{0};
		bench._source = &source;
		bench._trigger._object.store(nullptr, ::std::memory_order_release);
		probe_last_claims.store(0, ::std::memory_order_relaxed);
		probe_completed.store(false, ::std::memory_order_relaxed);

		::std::vector<manual_sender> children;
		children.push_back(manual_sender{&bench._trigger});

		auto operation = ex::connect(dynamic_when_all(::std::move(children)),
			counting_receiver{&completions, source.get_token()});
		ex::start(operation);   // 装回调 + start 孩子（孩子只登记，不完成）

		// 两个线程对齐后同时开火
		bench._done.store(0, ::std::memory_order_release);
		while (bench._ready.load(::std::memory_order_acquire) < 2) { }
		bench._ready.store(0, ::std::memory_order_release);
		bench._generation.store(round, ::std::memory_order_release);

		// 必须等两边都返回：thread_b 的 request_stop 还在跑时 source 不能析构
		while (bench._done.load(::std::memory_order_acquire) < 2) { }

		if (probe_last_claims.load(::std::memory_order_relaxed) >= 2) { ++claims; }

		auto const seen = completions.load(::std::memory_order_relaxed);
		if (seen >= 2)
		{
			++doubles;
			if (stop_at_first_hit)
			{
				auto const elapsed = ::std::chrono::duration_cast<::std::chrono::microseconds>(
					::std::chrono::steady_clock::now() - started).count();
				::std::printf("    第 %d 轮命中：receiver 被完成 %d 次（耗时 %lld us）\n"
					"    就此打住 —— 双重完成之后一切都是 UB，再统计没意义。\n",
					round, seen, (long long) elapsed);
				bench._quit.store(true, ::std::memory_order_relaxed);
				bench._generation.fetch_add(1, ::std::memory_order_release);
				thread_a.detach();
				thread_b.detach();
				::std::printf("  %-8s 前 %d 轮内就撞上了\n", label, round);
				return doubles;
			}
		}
	}

	bench._quit.store(true, ::std::memory_order_relaxed);
	bench._generation.fetch_add(1, ::std::memory_order_release);
	thread_a.join();
	thread_b.join();

	::std::printf("  %-8s %7d 轮 → receiver 被完成两次 %d 次 | 探针记到双重 claim %d 次\n",
		label, rounds, doubles, claims);
	// 探针模式下第二个 claimer 被拦住不完成，所以那边要看 claims；真实模式看 doubles。
	return probe_window_hook != nullptr || claims > 0 ? claims : doubles;
}

auto widen() -> void { ::std::this_thread::sleep_for(::std::chrono::microseconds{200}); }

int main()
{
#ifdef USE_PROBE
	::std::printf("【探针头】窗口里插了计数——窗口被这两条原子指令本身撑大了，数值偏高：\n");
	run_mode("natural", 100000, nullptr);
	auto const widened = run_mode("widened", 2000, &widen);
	::std::printf("\n把窗口显式撑开 200us 后命中 %d/2000 —— 这条路径%s。\n",
		widened, widened > 0 ? "可达 —— 还没修" : "打不中了 —— 修好了");
#else
	::std::printf("【真实头】一行没改，只看 receiver 被完成了几次：\n");
	auto const hits = run_mode("real", 50000, nullptr, true);
	::std::printf("\n结论：%s\n", hits > 0
		? "receiver 被完成两次 —— 还没修"
		: "5 万轮全绿 —— 修好了（未修版第 1 轮就中）");
#endif
	return 0;
}
