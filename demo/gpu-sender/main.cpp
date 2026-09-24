#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <thread>

#include <stdexec/execution.hpp>

#include "./context.h"
#include "./sender.h"

/// GPU sender 最小原型：推演里的"情形 A"——fill、copy、readback，不碰 swapchain。
///
/// 每个 case 打印回调在哪个线程上跑、提交了几次、插了几个 barrier，用来对照推演：
/// 录制回调在 start 的线程上同步执行；边界之后的续体在 reactor 线程上；
/// 一条链不论几个 record，都只 submit 一次。
namespace
{
auto thread_name() -> ::std::string
{
	static auto const main_thread = ::std::this_thread::get_id();
	if (::std::this_thread::get_id() == main_thread)
		return "main";
	auto stream = ::std::ostringstream{};
	stream << "thread " << ::std::this_thread::get_id();
	return stream.str();
}

auto expect(bool condition, char const* what) -> void
{
	::std::printf("  [%s] %s\n", condition ? "ok" : "FAILED", what);
	if (!condition)
		throw ::std::runtime_error{what};
}

/// schedule | record(fill) | then | record(copy)，直接 sync_wait。
///
/// 中间那个 `then` 转发环境，所以它和两个 record 一起留在录制时，拿到的是 `buffer_ref`；
/// 最后的 record 下游是 sync_wait，没有 encoder，于是它成为边界，值换成 `host_view`。
auto fused_chain(::gpu::context& ctx) -> void
{
	::std::puts("case 1: two records and a then, fused into one submit");
	auto source = ::gpu::host_buffer{ctx, 64};
	auto target = ::gpu::host_buffer{ctx, 64};
	auto const before = ctx.submissions();
	auto barriers = 0;

	auto work = ::stdexec::schedule(::gpu::scheduler{ctx})
		| ::gpu::record([&](::gpu::encoder& e)
		{
			::std::printf("  record(fill) runs on %s\n", thread_name().c_str());
			return e.fill(::gpu::ref(source), 0x2a2a2a2au);
		})
		| ::stdexec::then([](::gpu::buffer_ref filled)
		{
			// 录制时：filled 只是句柄，GPU 还没开始跑。这里能做的只有句柄上的变换。
			::std::printf("  then (between records) runs on %s, sees a buffer_ref\n", thread_name().c_str());
			return filled;
		})
		| ::gpu::record([&](::gpu::encoder& e, ::gpu::buffer_ref filled)
		{
			::std::printf("  record(copy) runs on %s\n", thread_name().c_str());
			auto copied = e.copy(filled, ::gpu::ref(target));
			barriers = e.barriers();
			return copied;
		});

	// 类型层面：同一个 sender，下游有没有 encoder，完成值就不一样。
	static_assert(::std::same_as<
		::stdexec::value_types_of_t<decltype(work), ::stdexec::env<>, ::gpu::pack, ::gpu::single_value_set_t>,
		::gpu::pack<::gpu::host_view>>);

	auto [view] = ::stdexec::sync_wait(::std::move(work)).value();
	auto const words = view.as<::std::uint32_t>();
	expect(words.size() == 16 && ::std::ranges::all_of(words, [](auto w) { return w == 0x2a2a2a2au; }),
		"target holds the filled pattern");
	expect(ctx.submissions() - before == 1, "one submit for the whole chain");
	// fill→copy 读 source 一个，copy 前 target 没有被访问过所以不插；host 读前再插一个。
	expect(barriers == 1, "one barrier between fill and copy (plus the host barrier at the boundary)");
}

/// schedule | record(fill) | then：then 直接连在 sync_wait 前面。
///
/// then 的接收者环境来自 sync_wait，没有 encoder，所以 record 成为边界：
/// then 在 GPU 做完以后才执行，拿到的是 `host_view`，而且跑在 reactor 线程上。
auto cpu_continuation(::gpu::context& ctx) -> void
{
	::std::puts("case 2: then after the boundary runs on the CPU, after the GPU");
	auto buffer = ::gpu::host_buffer{ctx, 16};
	auto work = ::stdexec::schedule(::gpu::scheduler{ctx})
		| ::gpu::record([&](::gpu::encoder& e) { return e.fill(::gpu::ref(buffer), 7u); })
		| ::stdexec::then([](::gpu::host_view view)
		{
			::std::printf("  then (after boundary) runs on %s, sees a host_view\n", thread_name().c_str());
			return view.as<::std::uint32_t>()[3];
		});
	auto [word] = ::stdexec::sync_wait(::std::move(work)).value();
	expect(word == 7u, "then reads the value the GPU wrote");
}

/// 停止只在 submit 之前生效：先请求停止，边界录完后不提交，直接 set_stopped。
auto stopped_before_submit(::gpu::context& ctx) -> void
{
	::std::puts("case 3: stop requested before submit");
	auto buffer = ::gpu::host_buffer{ctx, 16};
	auto source = ::stdexec::inplace_stop_source{};
	source.request_stop();
	auto const before = ctx.submissions();
	auto work = ::stdexec::schedule(::gpu::scheduler{ctx})
		| ::gpu::record([&](::gpu::encoder& e) { return e.fill(::gpu::ref(buffer), 1u); });
	auto result = ::stdexec::sync_wait(::stdexec::write_env(::std::move(work),
		::stdexec::prop{::stdexec::get_stop_token, source.get_token()}));
	expect(!result.has_value(), "completes with set_stopped");
	expect(ctx.submissions() == before, "nothing was submitted");
}

/// 录制回调抛异常：边界没有提交任何东西，错误立即传出。
auto error_while_recording(::gpu::context& ctx) -> void
{
	::std::puts("case 4: an exception while recording");
	auto const before = ctx.submissions();
	auto work = ::stdexec::schedule(::gpu::scheduler{ctx})
		| ::gpu::record([](::gpu::encoder&) -> ::gpu::buffer_ref { throw ::std::runtime_error{"record failed"}; });
	auto caught = false;
	try
	{
		(void)::stdexec::sync_wait(::std::move(work));
	}
	catch (::std::runtime_error const& error)
	{
		caught = ::std::string_view{error.what()} == "record failed";
	}
	expect(caught, "sync_wait rethrows the recording error");
	expect(ctx.submissions() == before, "nothing was submitted");
}
}

auto main() -> int
{
	try
	{
		auto ctx = ::gpu::context{};
		fused_chain(ctx);
		cpu_continuation(ctx);
		stopped_before_submit(ctx);
		error_while_recording(ctx);
		::std::puts("all cases passed");
		return 0;
	}
	catch (::std::exception const& error)
	{
		::std::fprintf(stderr, "error: %s\n", error.what());
	}
	catch (::VkResult result)
	{
		::std::fprintf(stderr, "error: VkResult %d\n", static_cast<int>(result));
	}
	return 1;
}
