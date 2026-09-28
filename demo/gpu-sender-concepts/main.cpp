#include <cstdio>
#include <stdexcept>
#include <string_view>

#include <stdexec/execution.hpp>

#include "./algorithms.h"
#include "./concepts.h"
#include "./image_state.h"
#include "./mock_vulkan.h"
#include "./swapchain.h"
#include "./tracked_value.h"

namespace
{
void expect(bool condition, char const* what)
{
	::std::printf("  [%s] %s\n", condition ? "ok" : "FAILED", what);
	if (!condition)
		throw ::std::runtime_error{what};
}

/// 第一部分：抽象层的验证，跟 Vulkan 完全无关——证明 tracked_resource/pending_resource
/// 这两个 concept 是通用的，不是围着 image/buffer 量身定做的。
namespace abstract_check
{
	struct counter_state
	{
		int value;
		bool operator==(counter_state const&) const = default;
	};

	// 满足 tracked_resource：resource()/state() 都是 std::copyable，transite() 返回 sender。
	struct counter_resource
	{
		int _id;
		counter_state _state;

		[[nodiscard]] int resource() const noexcept { return _id; }
		[[nodiscard]] counter_state state() const noexcept { return _state; }
		[[nodiscard]] auto transite(counter_state to) const { return ::stdexec::just(counter_resource{_id, to}); }
	};
	static_assert(::gpu::tracked_resource<counter_resource>);

	// 近似但不满足：缺 transite。
	struct missing_transite
	{
		[[nodiscard]] int resource() const noexcept { return 0; }
		[[nodiscard]] counter_state state() const noexcept { return {}; }
	};
	static_assert(!::gpu::tracked_resource<missing_transite>);

	// 近似但不满足：state() 按引用返回——`{ expr } -> std::copyable` 直接判否，
	// 因为 copyable 要求对象类型；这正是 tracked_resource 该拒绝的实现（见
	// concepts.h 顶部的说明），不是约束表达能力不够。
	struct uncopyable_state
	{
		uncopyable_state() = default;
		uncopyable_state(uncopyable_state const&) = delete;
		uncopyable_state& operator=(uncopyable_state const&) = delete;
	};
	struct bad_state_type
	{
		[[nodiscard]] int resource() const noexcept { return 0; }
		[[nodiscard]] uncopyable_state const& state() const noexcept { static uncopyable_state s; return s; }
		[[nodiscard]] auto transite(uncopyable_state const&) const { return ::stdexec::just(); }
	};
	static_assert(!::gpu::tracked_resource<bad_state_type>);

	// pending_resource：只需要 consume_external 返回 sender，跟 resource()/state() 无关。
	struct counter_pending
	{
		int _id;
		[[nodiscard]] auto consume_external() const { return ::stdexec::just(counter_resource{_id, counter_state{0}}); }
	};
	static_assert(::gpu::pending_resource<counter_pending>);
	static_assert(!::gpu::pending_resource<missing_transite>);

	void run()
	{
		::std::puts("part 1: abstract tracked_resource/pending_resource, no Vulkan involved");
		auto work = ::stdexec::just(counter_resource{1, counter_state{0}})
			| ::gpu::transition(counter_state{7});
		auto [result] = ::stdexec::sync_wait(::std::move(work)).value();
		expect(result._state.value == 7, "transition drives a fully generic tracked_resource, not just Vulkan images");
	}
}

/// 第二部分：真实场景——acquire -> consume -> transition -> clear -> transition ->
/// submit -> present，跟本轮设计讨论里逐行对照手写 Vulkan 的那条链完全一致。
namespace vulkan_scenario
{
	void run()
	{
		::std::puts("part 2: acquire -> consume -> transition -> clear -> transition -> submit -> present");
		auto sc = ::gpu::mock::swapchain{};
		auto dom = ::gpu::domain{};

		auto work = ::gpu::acquire(sc)
			| ::gpu::consume()
			| ::gpu::transition(::gpu::color_attachment{})
			| ::stdexec::then([&dom](auto const& item)
			{
				::gpu::mock::cmd_clear_color_image(dom.command_buffer(), ::gpu::resource(item));
				return item;
			})
			| ::gpu::transition(::gpu::present_src{})
			| ::gpu::submit(dom)
			| ::gpu::present(sc)
			| ::stdexec::write_env(::stdexec::prop{::gpu::get_domain, &dom});

		::stdexec::sync_wait(::std::move(work));

		// consume() 落在 domain 的录制链里，image_available 被记成 submit 时要等的信号，
		// 不产生独立日志行——这正是"CUDA 式"的 consume_external：谁在消费它，决定它是
		// 记一条 wait 还是真的去等；main.cpp 这条链从头到尾都在 domain 的录制链里，
		// 所以这次跑的是前一种。
		auto const& log = ::gpu::mock::global_log.lines();
		expect(log.size() == 6, "六条日志：acquire、两条 barrier、一次 clear、一次 submit、一次 present");
		expect(log[0].starts_with("acquire_next_image"), "1: acquire");
		expect(log[1].starts_with("cmd_pipeline_barrier"), "2: undefined -> color_attachment 的 barrier");
		expect(log[2].starts_with("cmd_clear_color_image"), "3: clear（此时 layout 已经是 color_attachment_optimal）");
		expect(log[3].starts_with("cmd_pipeline_barrier"), "4: color_attachment -> present_src 的 barrier");
		expect(log[4].starts_with("queue_submit") && log[4].find("waits=1") != ::std::string::npos,
			"5: submit 时带着 acquire 借来的那个信号（waits=1），不是靠一次单独的 host_wait 表达");
		expect(log[5].starts_with("queue_present"), "6: present");
	}

	/// 同一个 pending_value，consume_external 消费的方式随消费者而变，不随值本身而变：
	/// 上面那条链从头到尾都在 domain 里，consume() 走的是"记一条 wait"；这里故意不给
	/// 任何 domain，直接 sync_wait 消费同一个 pending_value，consume_external 就落到
	/// 真正的 host_wait 那一支——跟 CUDA 里同一个 event，被 cudaStreamWaitEvent 还是
	/// cudaEventSynchronize 消费完全取决于调用方，是同一个道理。
	void run_cpu_side_consume()
	{
		::std::puts("part 3: the same pending_value, consumed with no domain in scope");
		auto sc = ::gpu::mock::swapchain{};
		auto const before = ::gpu::mock::global_log.lines().size();

		auto work = ::gpu::acquire(sc) | ::gpu::consume();
		::stdexec::sync_wait(::std::move(work));

		auto const& log = ::gpu::mock::global_log.lines();
		expect(log.size() == before + 2, "两条日志：acquire、host_wait");
		expect(log[before].starts_with("acquire_next_image"), "1: acquire");
		expect(log[before + 1].starts_with("host_wait"), "2: 没有 domain 在录制，consume_external 走真正的等待，不是记一条 wait");
	}
}
}

int main()
{
	try
	{
		abstract_check::run();
		vulkan_scenario::run();
		vulkan_scenario::run_cpu_side_consume();
		::std::puts("all cases passed");
		return 0;
	}
	catch (::std::exception const& error)
	{
		::std::fprintf(stderr, "error: %s\n", error.what());
		return 1;
	}
}
