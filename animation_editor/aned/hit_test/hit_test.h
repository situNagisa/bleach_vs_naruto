#pragma once

#include <concepts>
#include <ranges>

#include <entt/entt.hpp>
#include <glm/glm.hpp>


#include "../timeline/system.h"

#include "./box.h"

namespace aned::controller
{
	template<::std::output_iterator<::entt::handle> Outter>
	Outter hit_test_impl(
		::entt::handle handle
		, ::std::size_t current_frame
		, ::boost::geometry::model::d2::point_xy<float> point
		, ::glm::mat3x3 const& parent_matrix
		, Outter out
	) noexcept
	{
		using namespace ::std::views;
		auto&& local_matrix = handle.any_of<::glm::mat3x3>() ? handle.get<::glm::mat3x3>() : ::glm::mat3x3(1.0f);
		auto matrix = parent_matrix * local_matrix;

		if (handle.any_of<component::hit_box>())
		{
			namespace bg = ::boost::geometry;
			auto&& box = handle.get<component::hit_box>();
			auto transformed_point = ::glm::inverse(matrix) * ::glm::vec3(point.x(), point.y(), 1.0f);
			if (bg::within(
				bg::model::d2::point_xy<float>{ transformed_point.x, transformed_point.y }
				, box.box
				))
			{
				*out = handle;
				++out;
			}
		}

		if (handle.any_of<component::timeline_system>())
		{
			for (auto&& frames : handle.get<component::timeline_system>().frames() | reverse | join | drop(current_frame))
			{
				if (!frames)
					continue;
				for (auto h : frames->keyframe->displays | reverse)
				{
					out = controller::hit_test_impl(
						h
						, current_frame - frames->keyframe_index
						, point
						, matrix
						, out
					);
				}
				break;
			}
		}
		return out;
	}

	inline auto hit_test(
		::entt::handle handle
		, ::std::size_t current_frame
		, ::boost::geometry::model::d2::point_xy<float> point
		, ::glm::mat3x3 const& parent_matrix = ::glm::mat3x3(1.0f)
	) noexcept
	{
		auto results = ::std::vector<::entt::handle>{};
		controller::hit_test_impl(
			handle
			, current_frame
			, point
			, parent_matrix
			, ::std::back_inserter(results)
		);
		return results;
	}
}