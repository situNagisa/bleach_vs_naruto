#pragma once

/// 与计算方式无关的小工具：被优化掉的成员的占位类型、copy_cvref_t、to_array。

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace sdag::meta
{

/// 被优化掉的元素用它占位：indexed_tuple 里它是真正的 0 字节空基类。
struct empty
{
};

/// 一个类里有多个被优化掉的成员时，各用一个互不相同的空类型：两个同类型的空成员必须有不同地址，
/// no_unique_address 也压不掉其中一个。
template<class Tag>
struct empty_for
{
};


/// To 带上 From 的 const 与值类别（std::forward_like 的类型版）。
template<class From, class To>
using copy_cvref_t = decltype(::std::forward_like<From>(::std::declval<To&>()));

/// 把编译期的 vector 固定成 std::array（长度要先用另一次常量求值算出来）。
template<::std::size_t N, class T>
[[nodiscard]] constexpr ::std::array<T, N> to_array(::std::vector<T> const& values)
{
	::std::array<T, N> result{};
	for (::std::size_t index = 0; index < N; ++index)
	{
		result[index] = values[index];
	}
	return result;
}

}
