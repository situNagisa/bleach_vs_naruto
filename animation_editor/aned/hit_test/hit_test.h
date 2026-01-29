#pragma once

#include <concepts>
#include <ranges>
#include <generator>
#include <list>

#include <entt/entt.hpp>
#include <glm/glm.hpp>


#include "../timeline/system.h"

#include "./box.h"

namespace aned::controller
{
	inline ::std::generator<::std::list<::entt::handle>&> hit_test_impl(
		::entt::handle handle
		, ::std::size_t current_frame
		, ::boost::geometry::model::d2::point_xy<float> point
		, ::glm::mat3x3 const& parent_matrix
		, ::std::list<::entt::handle>& stack
	) noexcept
	{
		stack.emplace_back(handle);
		{
			using namespace ::std::views;
			namespace bg = ::boost::geometry;
			auto&& local_matrix = handle.any_of<::glm::mat3x3>() ? handle.get<::glm::mat3x3>() : ::glm::mat3x3(1.0f);
			auto matrix = parent_matrix * local_matrix;

			if (handle.any_of<component::hit_box>())
			{
				auto&& box = handle.get<component::hit_box>();
				auto transformed_point = ::glm::inverse(matrix) * ::glm::vec3(point.x(), point.y(), 1.0f);
				if (bg::within(bg::model::d2::point_xy<float>{ transformed_point.x, transformed_point.y }, box.box))
				{
					co_yield stack;
				}
				if (handle.any_of<component::hit_box_depend_children_tag>())
					goto clean_up;
			}

			if (handle.any_of<component::timeline_system>())
			{
				for (auto&& frames : handle.get<component::timeline_system>().frames() | reverse | join | drop(current_frame))
				{
					if (!frames)
						continue;
					for (auto h : frames->keyframe->displays | reverse)
					{
						co_yield ::std::ranges::elements_of(controller::hit_test_impl(h, current_frame - frames->keyframe_index, point, matrix, stack));
					}
					break;
				}
			}
		}
	clean_up:;
		stack.pop_back();
		co_return;
	}

	inline ::std::generator<::std::list<::entt::handle>&> hit_test(
		::entt::handle handle
		, ::std::size_t current_frame
		, ::boost::geometry::model::d2::point_xy<float> point
		, ::glm::mat3x3 const& parent_matrix = ::glm::mat3x3(1.0f)
	) noexcept
	{
		::std::list<::entt::handle> buffer{};
		auto results = ::std::vector<::entt::handle>{};
		co_yield ::std::ranges::elements_of(controller::hit_test_impl(handle, current_frame, point, parent_matrix, buffer));
		co_return;
	}
}