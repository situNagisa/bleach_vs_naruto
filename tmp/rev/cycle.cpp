// 构建期成环要当场断言，不能无限递归爆栈。
// 单独一个 TU：它会 abort，不该混进主 demo。
#include <cstdio>
#include <cstdint>
#include <vector>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "./dynamic_when_all.h"
#include "./entities.h"
#include "./node_sender.h"

struct frame
{
	::std::vector<node_sender> _roots;
	entities<frame> _entities;

	frame() : _entities(*this) {}
};

// 两个 entity 互相要对方的 task —— 谁先被构建都会绕回自己
struct knot_a
{
	struct task { int _value = 0; };
	auto build_task(frame& context, task_builder builder) -> void;
};

struct knot_b
{
	struct task { int _value = 0; };
	auto build_task(frame& context, task_builder builder) -> void;
};

auto knot_a::build_task(frame& context, task_builder builder) -> void
{
	if (auto other = context._entities.entity<knot_b>())
	{
		static_cast<void>(other->task<knot_b::task>());
	}
	builder.emplace<task>();
}

auto knot_b::build_task(frame& context, task_builder builder) -> void
{
	if (auto other = context._entities.entity<knot_a>())
	{
		static_cast<void>(other->task<knot_a::task>());
	}
	builder.emplace<task>();
}

int main()
{
	auto left = knot_a{};
	auto right = knot_b{};

	auto context = frame{};
	context._entities.add(left);
	context._entities.add(right);

	::std::printf("about to build a cyclic pair; expect an assertion\n");
	::std::fflush(stdout);

	context._entities.build_all();

	::std::printf("FAIL: no assertion fired\n");
	return 1;
}
