#pragma once

#include <vector>
#include <ranges>

#include <entt/entt.hpp>

struct container
{
	::std::vector<::entt::handle> _children{};

	constexpr auto children() const { return _children | ::std::views::all; }
};