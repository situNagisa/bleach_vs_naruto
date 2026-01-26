#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <imgui.h>

#include "../component/timeline/system.h"
#include "../component/movie_clip.h"
#include "../component/image.h"

void render_impl(::entt::handle handle, ::glm::mat3x3 const& parent_matrix) noexcept
{
	using namespace ::std::views;
	auto&& local_matrix = handle.any_of<::glm::mat3x3>() ? handle.get<::glm::mat3x3>() : ::glm::mat3x3(1.0f);
	auto matrix = parent_matrix * local_matrix;
	if (handle.any_of<image>())
	{
		auto&& img = handle.get<image>();
		auto position = ::glm::vec3(0, 0, 1) * matrix;
		auto size = ::glm::vec3(img.width, img.height, 1) * matrix;

		::ImGui::SetCursorScreenPos({ position.x, position.y });
		::ImGui::Image(static_cast<::ImTextureID>(*img.texture.get()), { size.x, size.y });
		// ImU32 border_color = IM_COL32(0, 255, 0, 255);
		// float border_thickness = 2.0f;
		// draw_list->AddRect(ImVec2(screen_x, screen_y), ImVec2(screen_x + screen_w, screen_y + screen_h),
		// 	border_color, 0.0f, 15, border_thickness);
	}
	if (handle.any_of<timeline_system>())
	{
		auto current_frame = handle.any_of<movie_clip>() ? handle.get<movie_clip>().current_frame : 0;
		for (auto&& frames : handle.get<timeline_system>().keyframes() | drop(current_frame) | take(1))
		{
			for (auto&& ff : frames)
			{
				if (!ff)
					continue;
				for (auto h : ff->displays)
				{
					::render_impl(h, matrix);
				}
			}
		}
	}
}

void render(::entt::handle handle) noexcept
{
	::render_impl(handle, ::glm::mat3x3(1.0f));
}