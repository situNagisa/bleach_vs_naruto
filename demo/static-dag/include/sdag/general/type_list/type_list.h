#pragma once

/// type_list<Ts...> 与按下标取元素的 at_t<I, List>。编译期计算里所有"一组类型"都用它表示。

#include <cstddef>

namespace sdag::general
{

template<class... Ts>
struct type_list
{
	static constexpr ::std::size_t size = sizeof...(Ts);
};

template<::std::size_t I, class List>
struct at;

template<::std::size_t I, class... Ts>
struct at<I, type_list<Ts...>>
{
	using type = Ts...[I];
};

template<::std::size_t I, class List>
using at_t = typename at<I, List>::type;

}
