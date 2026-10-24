#pragma once

/// 反射模式下各个元函数共用的基础（模板模式下这个文件不定义任何东西）。
///
/// 写法约定（凡是有两种实现的文件都一样）：
///   - 每个文件对外只提供一两个名字（别名模板或变量模板），两种实现用 #if SDAG_REFLECTION 隔开，对外名字完全相同；
///   - 反射实现：类型用 std::meta::info 表示，类型列表就是 std::vector<info>（infos），运算都是普通的 consteval 函数，
///     对外的名字再用 [: :] 把结果拼回类型；
///   - 跨文件复用的 consteval 函数放在 general::refl，只在本文件里用的实现细节放在 detail。

#include "../../config.h"

#if SDAG_REFLECTION

#include <array>
#include <cstddef>
#include <meta>
#include <vector>

#include "type_list.h"

namespace sdag::general
{

inline namespace SDAG_ABI_NAMESPACE
{

namespace refl
{

using info = ::std::meta::info;
using infos = ::std::vector<info>;

/// 模板特化的实参（先去掉别名）。
[[nodiscard]] consteval infos arguments(info type)
{
	return ::std::meta::template_arguments_of(::std::meta::dealias(type));
}

/// type_list<types...>
[[nodiscard]] consteval info list(infos const& types)
{
	return ::std::meta::substitute(^^type_list, types);
}

[[nodiscard]] consteval bool same(info left, info right)
{
	return ::std::meta::dealias(left) == ::std::meta::dealias(right);
}

/// 去别名、去 cv / 引用。对 sender 类型做任何检查之前都先做这一步。
[[nodiscard]] consteval info plain(info type)
{
	return ::std::meta::remove_cvref(::std::meta::dealias(type));
}

/// type 是不是模板 primary 的特化（先 plain）。
[[nodiscard]] consteval bool specializes(info type, info primary)
{
	type = plain(type);
	return ::std::meta::has_template_arguments(type) && ::std::meta::template_of(type) == primary;
}

/// libstdc++ 的 std::array<T, 0> 没有 consteval 的 operator[]，装不了 consteval-only 的 info；N 为 0 时留一个占位元素。
template<::std::size_t N>
using info_array = ::std::array<info, N == 0 ? 1 : N>;

}

}

}

#endif
