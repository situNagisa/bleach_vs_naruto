#pragma once

/// 测试用的最小断言：不引第三方框架，失败计数非零时进程返回非零。

#include <cstdlib>
#include <print>
#include <source_location>
#include <string_view>

namespace test
{

inline int failures = 0;
inline int checks = 0;

inline void check(bool condition, ::std::string_view what, ::std::source_location where = ::std::source_location::current())
{
	++checks;
	if (!condition)
	{
		++failures;
		::std::println("  失败: {} ({}:{})", what, where.file_name(), where.line());
	}
}

inline void section(::std::string_view name)
{
	::std::println("[{}]", name);
}

inline int report()
{
	::std::println("{} 项检查，{} 项失败", checks, failures);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}
