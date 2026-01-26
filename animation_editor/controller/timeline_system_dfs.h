#pragma once

#include "../component/timeline/system.h"
#include "../component/movie_clip.h"

constexpr void dfs_impl(timeline_system& t, movie_clip clip, ::std::size_t deep, auto f) noexcept;
constexpr void dfs_impl(::entt::handle handle, ::std::size_t deep, auto f) noexcept
{
	f(deep, 0, handle);
	if (!handle.any_of<timeline_system>())
		return;
	::dfs_impl(handle.get<timeline_system>(), handle.any_of<movie_clip>() ? handle.get<movie_clip>() : movie_clip{ 0 }, deep + 1, f);
}

constexpr void dfs_impl(timeline_system& t, movie_clip clip, ::std::size_t deep, auto f) noexcept
{
	::std::size_t i = 0;
	for (::std::optional<timeline::keyframe> const& ff : t.keyframes()[clip.current_frame])
	{
		if (!ff)
			continue;
		for (auto h : ff->displays)
		{
			f(deep, i, h);
			::dfs_impl(h, deep + 1, f);
			++i;
		}
	}
}

constexpr void dfs(::entt::handle handle, auto f) noexcept
{
	::dfs_impl(handle, 0, f);
}