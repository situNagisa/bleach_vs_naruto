#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <imgui.h>

#include "./movie/movie_clip.h"
#include "./image/image.h"

#include "./timeline/system.h"

namespace aned::controller
{
	inline void render_canvas(::entt::handle handle, ::glm::mat3x3 const& parent_matrix = ::glm::mat3x3(1.0f)) noexcept
	{
		using namespace ::std::views;
		auto&& local_matrix = handle.any_of<::glm::mat3x3>() ? handle.get<::glm::mat3x3>() : ::glm::mat3x3(1.0f);
		auto matrix = parent_matrix * local_matrix;
		if (handle.any_of<component::image>())
		{
			auto&& img = handle.get<component::image>();
			auto position = matrix * ::glm::vec3(0, 0, 1);
			auto size = matrix * ::glm::vec3(img.width, img.height, 0.f);

			::ImGui::SetCursorScreenPos({ position.x, position.y });
			::ImGui::Image(static_cast<::ImTextureID>(img.texture.value), { size.x, size.y });
			// ImU32 border_color = IM_COL32(0, 255, 0, 255);
			// float border_thickness = 2.0f;
			// draw_list->AddRect(ImVec2(screen_x, screen_y), ImVec2(screen_x + screen_w, screen_y + screen_h),
			// 	border_color, 0.0f, 15, border_thickness);
		}
		if (handle.any_of<component::timeline_system>())
		{
			auto current_frame = handle.any_of<component::movie_clip>() ? handle.get<component::movie_clip>().current_frame : 0;
			for (auto&& frames : handle.get<component::timeline_system>().frames() | drop(current_frame) | take(1))
			{
				for (auto&& ff : frames)
				{
					if (!ff)
						continue;
					for (auto h : ff->keyframe->displays)
					{
						controller::render_canvas(h, matrix);
					}
				}
			}
		}
	}
}
