#pragma once

#include <imgui.h>

namespace aned::timeline_system
{
	struct theme
	{
		// Panel colors
		struct
		{
			ImU32 background;
		} panel;

		// Header colors and sizes
		struct
		{
			ImU32 background;
			ImU32 border;
			float height;
		} header;

		// Frame grid colors and sizes
		struct
		{
			ImU32 major_line;
			ImU32 minor_line;
			int major_interval;
		} frame_grid;

		// Layer colors
		struct
		{
			ImU32 background_even;
			ImU32 background_odd;
			ImU32 selected_background;
			ImU32 label_text;
			float label_padding_x;
			ImU32 border_right;
			ImU32 border_bottom;
		} layer;

		// Keyframe marker colors and sizes
		struct
		{
			ImU32 duration_bar_fill;
			ImU32 duration_bar_border;
			ImU32 marker_fill;
			ImU32 marker_border;
			float marker_size;
			float height_ratio;
			float border_thickness;
			float padding;
			float rounding;
		} keyframe;

		// Playhead colors
		struct
		{
			ImU32 color;
			float thickness;
		} playhead;

		// Frame number text color and positioning
		struct
		{
			ImU32 color;
			float offset_x;
			float offset_y;
		} frame_number;

		// UI layout and spacing
		struct
		{
			float layer_height;
			float frame_padding_x;
			float frame_padding_y;
			float item_spacing;
		} layout;

		// Selection and interaction
		struct
		{
			ImU32 frame_selected_color;
			ImU32 frame_hover_color;
			ImU32 layer_hover_color;
		} interaction;

		constexpr static auto visual_studio_dark() noexcept
		{
			theme result{};

			// Panel - dark background
			result.panel.background = IM_COL32(37, 37, 38, 255);

			// Header - slightly lighter
			result.header.background = IM_COL32(45, 45, 48, 255);
			result.header.border = IM_COL32(63, 63, 70, 255);
			result.header.height = 20.0f;

			// Frame grid - subtle lines
			result.frame_grid.major_line = IM_COL32(100, 100, 100, 255);
			result.frame_grid.minor_line = IM_COL32(62, 62, 66, 255);
			result.frame_grid.major_interval = 5;

			// Layer styling
			result.layer.background_even = IM_COL32(45, 45, 48, 255);
			result.layer.background_odd = IM_COL32(51, 51, 55, 255);
			result.layer.selected_background = IM_COL32(0, 102, 204, 200);  // VS Blue
			result.layer.label_text = IM_COL32(220, 220, 220, 255);
			result.layer.label_padding_x = 5.0f;
			result.layer.border_right = IM_COL32(63, 63, 70, 255);
			result.layer.border_bottom = IM_COL32(63, 63, 70, 255);

			// Keyframe styling - orange accent (VS orange)
			result.keyframe.duration_bar_fill = IM_COL32(206, 145, 120, 180);
			result.keyframe.duration_bar_border = IM_COL32(206, 145, 120, 255);
			result.keyframe.marker_fill = IM_COL32(220, 140, 50, 255);
			result.keyframe.marker_border = IM_COL32(220, 140, 50, 255);
			result.keyframe.marker_size = 2.0f;
			result.keyframe.height_ratio = 0.8f;
			result.keyframe.border_thickness = 1.5f;
			result.keyframe.padding = 1.0f;
			result.keyframe.rounding = 15.0f;

			// Playhead - red accent
			result.playhead.color = IM_COL32(240, 101, 101, 255);
			result.playhead.thickness = 2.0f;

			// Frame number text
			result.frame_number.color = IM_COL32(200, 200, 200, 255);
			result.frame_number.offset_x = 2.0f;
			result.frame_number.offset_y = 3.0f;

			// Layout defaults
			result.layout.layer_height = 24.0f;
			result.layout.frame_padding_x = 0.0f;
			result.layout.frame_padding_y = 0.0f;
			result.layout.item_spacing = 0.0f;

			// Interaction colors
			result.interaction.frame_selected_color = IM_COL32(100, 200, 255, 200);
			result.interaction.frame_hover_color = IM_COL32(100, 150, 200, 150);
			result.interaction.layer_hover_color = IM_COL32(70, 70, 80, 200);

			return result;
		}

		constexpr static auto visual_studio_light() noexcept
		{
			auto light_theme = theme::visual_studio_dark();

			// Customize colors for a light theme
			light_theme.panel.background = IM_COL32(240, 240, 240, 255);
			light_theme.header.background = IM_COL32(230, 230, 230, 255);
			light_theme.header.border = IM_COL32(180, 180, 180, 255);

			light_theme.frame_grid.major_line = IM_COL32(150, 150, 150, 255);
			light_theme.frame_grid.minor_line = IM_COL32(200, 200, 200, 255);

			light_theme.layer.background_even = IM_COL32(245, 245, 245, 255);
			light_theme.layer.background_odd = IM_COL32(235, 235, 235, 255);
			light_theme.layer.selected_background = IM_COL32(100, 150, 200, 200);
			light_theme.layer.label_text = IM_COL32(50, 50, 50, 255);

			return light_theme;
		}
	};
}
