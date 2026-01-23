#pragma once

#include <entt/entt.hpp>
#include <ranges>

#include "./container.h"

constexpr void dfs_impl(container& c, ::std::size_t deep, auto f) noexcept
{
	for (auto [i, h] : c.children() | ::std::views::enumerate)
	{
		f(deep, i, h);
		if (!h.any_of<container>())
			continue;
		::dfs_impl(h.get<container>(), deep + 1, f);
	}
}

constexpr void dfs(::entt::handle handle, auto f) noexcept
{
	f(0, 0, handle);
	if (!handle.any_of<container>())
		return;
	::dfs_impl(handle.get<container>(), 1, f);
}