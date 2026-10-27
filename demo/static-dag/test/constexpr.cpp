/// 常量求值：整张图在 static_assert 里连接、启动、完成。
///
/// 常量求值止步于原子量与停止源：入度计数、汇点计数、状态字是 std::atomic，停止源是 inplace_stop_source，碰到就不是常量表达式。
/// 所以这里只覆盖不碰它们的图：只有一个汇点、没有入度 > 1 的节点、不会失败。节点 sender 本身也要能常量求值（just / then）。
/// 只编译不运行：编过就是通过。

#include <utility>

#include <stdexec/execution.hpp>

#include <sdag/sdag.h>

namespace
{

enum class key
{
	a, b, c, d,
};
inline constexpr key a = key::a;
inline constexpr key b = key::b;
inline constexpr key c = key::c;
inline constexpr key d = key::d;

/// 常量求值里也能用的接收者：结果写进一个局部变量。
struct result_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	int* _out;

	constexpr void set_value(int value) noexcept
	{
		*_out = value;
	}

	constexpr void set_value(int x, int y) noexcept
	{
		*_out = x * 100 + y;
	}

	template<class Error>
	constexpr void set_error(Error&&) noexcept
	{
		*_out = -1;
	}

	constexpr void set_stopped() noexcept
	{
		*_out = -2;
	}

	[[nodiscard]] constexpr ::stdexec::env<> get_env() const noexcept
	{
		return {};
	}
};

template<class Graph>
constexpr int run(Graph&& graph)
{
	int out = 0;
	auto operation = ::stdexec::connect(::std::forward<Graph>(graph), result_receiver{&out});
	::stdexec::start(operation);
	return out;
}

// 链：唯一汇点直接转发，不会失败（没有停止源）
static_assert(run(::sdag::dag<key>(
	::sdag::make<::sdag::basic_node{a}>(::stdexec::just(1)),
	::sdag::make<::sdag::basic_node{b, ::sdag::let(a)}>(::stdexec::then([](int x) noexcept { return x + 1; })),
	::sdag::make<::sdag::basic_node{c, ::sdag::let(b)}>(::stdexec::then([](int x) noexcept { return x * 3; })))) == 6);

// move 链：值槽复用（a 与 c 共用一块存储，在常量求值里切换在世的成员）
static_assert(run(::sdag::dag<key>(
	::sdag::make<::sdag::basic_node{a}>(::stdexec::just(1)),
	::sdag::make<::sdag::basic_node{b, ::sdag::move(a)}>(::stdexec::then([](int x) noexcept { return x + 1; })),
	::sdag::make<::sdag::basic_node{c, ::sdag::move(b)}>(::stdexec::then([](int x) noexcept { return x * 3; })),
	::sdag::make<::sdag::basic_node{d, ::sdag::move(c)}>(::stdexec::then([](int x) noexcept { return x - 1; })))) == 5);

// 空图
static_assert([]
{
	struct empty_receiver
	{
		using receiver_concept = ::stdexec::receiver_t;
		bool* _done;
		constexpr void set_value() noexcept { *_done = true; }
		[[nodiscard]] constexpr ::stdexec::env<> get_env() const noexcept { return {}; }
	};
	bool done = false;
	auto operation = ::stdexec::connect(::sdag::dag<key>(), empty_receiver{&done});
	::stdexec::start(operation);
	return done;
}());

}

int main()
{
}
