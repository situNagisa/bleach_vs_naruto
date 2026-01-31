#pragma once

#include <entt/entt.hpp>

namespace aned::component
{
	struct display_node
	{
		::entt::handle parent{};
	};
}

namespace aned::haob
{
	struct display_object
	{
		::entt::handle _handle{};

		display_object(::entt::handle handle)
			: _handle{ handle }
		{
			BOOST_ASSERT(_handle.valid());
		}

		auto valid() const noexcept { return _handle.valid() && _handle.all_of<component::display_node>(); }
		auto parent() const noexcept { return _handle.get<component::display_node>().parent; }
		auto handle() const noexcept { return _handle; }
	};
}