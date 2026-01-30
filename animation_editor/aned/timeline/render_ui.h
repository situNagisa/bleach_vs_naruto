#pragma once

#include <cstddef>
#include <string>
#include <ranges>
#include <algorithm>
#include <optional>

#include <imgui.h>

#include "../movie/play_data.h"

#include "./system.h"
#include "./theme.h"

namespace aned::controller
{
	struct render_timeline_context
	{
		component::timeline_system const* system;
		component::play_data const* play_data;
		timeline_system::theme const* theme{};

		// Selection state (could be extended for range selection)
		mutable ::std::optional<::std::size_t> selected_frame;

		// Frame navigation
		::std::size_t start_frame_index{};
		float frame_width = 15.f;  // Width of each frame cell

		struct {
			ImGuiSelectionBasicStorage frame_header{};
			ImGuiSelectionBasicStorage frame{};
			ImGuiSelectionBasicStorage label{};
		} selection{};
	};

	inline void render_timeline_ui(render_timeline_context& context)
	{
		auto const& theme = *context.theme;

		auto visible_frame_count = ::std::max<::std::size_t>(context.system->frames().size(), 1);
		if (ImGui::BeginTable(
			"timeline"
			, 1 + visible_frame_count
			, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable
			| ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoClip
			| ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX
		))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2());
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());

			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableSetupColumn("folder", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);
			for (auto n : ::std::views::iota(0u, static_cast<::std::size_t>(visible_frame_count)))
			{
				ImGui::TableSetupColumn(
					::std::format("##{}-0", n).c_str()
					, ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_WidthFixed
					, context.frame_width
				);
			}
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

			ImGui::TableSetColumnIndex(0);
			ImGui::TableHeader(ImGui::TableGetColumnName(0));

			auto flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d;
			auto fh_ms_io = ImGui::BeginMultiSelect(flags, context.selection.frame_header.Size, visible_frame_count);
			context.selection.frame_header.ApplyRequests(fh_ms_io);
			for (auto n : ::std::views::iota(0u, static_cast<::std::size_t>(visible_frame_count)))
			{
				ImGui::TableSetColumnIndex(n + 1);
				bool item_is_selected = context.selection.frame_header.Contains((ImGuiID)n);
				ImGui::SetNextItemSelectionUserData(n);
				auto frame_index = n + context.start_frame_index + 1;
				auto label = n % theme.frame_grid.major_interval ? ::std::format("##0-{}", frame_index) : ::std::format("{}", frame_index);
				ImGui::Selectable(label.c_str(), item_is_selected, {}, { });
			}
			fh_ms_io = ImGui::EndMultiSelect();
			context.selection.frame_header.ApplyRequests(fh_ms_io);

			// auto label_ms_io = ImGui::BeginMultiSelect(flags, context.selection.label.Size, context.system->layers().size());
			// context.selection.label.ApplyRequests(label_ms_io);
			auto frame_size = ::std::ranges::fold_left(
				context.system->layers() | ::std::views::transform(&timeline_system::timeline_layer::timeline) | ::std::views::transform(::std::ranges::size)
				, 0u
				, ::std::plus{}
			);
			auto frame_ms_io = ImGui::BeginMultiSelect(flags, context.selection.frame.Size, frame_size);
			context.selection.frame.ApplyRequests(frame_ms_io);
			for (auto&& [idx, layer] : context.system->layers() | ::std::views::enumerate)
			{
				ImGui::TableNextRow();
				{
					ImGui::TableSetColumnIndex(0);
					// bool item_is_selected = context.selection.label.Contains((ImGuiID)idx);
					auto item_is_selected = false;
					ImGui::SetNextItemSelectionUserData(idx);
					ImGui::Selectable(layer.name.c_str(), item_is_selected, {}, { 0.f, context.theme->layout.layer_height });
				}

				for (auto [i, frame] : layer.timeline | ::std::views::enumerate)
				{
					ImGui::TableSetColumnIndex(i + 1);

					auto region_avail = ImGui::GetContentRegionAvail();
					auto draw_list = ImGui::GetWindowDrawList();
					auto start = ImGui::GetCursorScreenPos();

					ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, context.theme->keyframe.fill);

					auto id = (static_cast<::std::size_t>(idx) << 21) | static_cast<::std::size_t>(i);
					bool item_is_selected = context.selection.frame.Contains((ImGuiID)id);
					ImGui::SetNextItemSelectionUserData(id);

					auto frame_index = i + context.start_frame_index + 1;
					auto label = ::std::format("##{}-{}", idx + 1, frame_index);
					ImGui::Selectable(label.c_str(), item_is_selected, {}, { context.frame_width, context.theme->layout.layer_height });

					if (i == frame.keyframe_index)
					{
						// draw_list->AddRectFilled(
						// 	{ start.x, start.y }
						// 	, { start.x + region_avail.x, start.y + region_avail.y }
						// 	, IM_COL32(255, 255, 255, 255)
						// );
						draw_list->AddCircleFilled(
							{ start.x + region_avail.x / 2, start.y + context.theme->layout.layer_height / 4 }
							, context.theme->keyframe.marker_radius
							, context.theme->keyframe.marker_fill
						);
					}
				}
			}
			frame_ms_io = ImGui::EndMultiSelect();
			context.selection.frame.ApplyRequests(frame_ms_io);
			// label_ms_io = ImGui::EndMultiSelect();
			// context.selection.label.ApplyRequests(label_ms_io);

			ImGui::PopStyleVar(2);

			ImGui::EndTable();
		}
		return;
		// ===== FRAME HEADER SECTION =====
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2());
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

			// Header container
			ImVec2 header_region_avail = ImGui::GetContentRegionAvail();
			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			ImVec2 header_start = ImGui::GetCursorScreenPos();
			
			// Draw header background
			ImVec2 header_end(header_start.x + header_region_avail.x, header_start.y + theme.header.height);
			draw_list->AddRectFilled(header_start, header_end, theme.header.background);
			draw_list->AddRect(header_start, header_end, theme.header.border, 0.0f, 0);

			// Reserve space for header
			ImGui::Dummy(ImVec2(header_region_avail.x, theme.header.height));

			// Now draw frame numbers and grid lines on the header we just drew
			{
				// Draw vertical separator between label and timeline areas
				float label_width = 150.0f;  // Standard label width
				ImVec2 separator_pos(header_start.x + label_width, header_start.y);
				draw_list->AddLine(separator_pos, ImVec2(separator_pos.x, header_end.y), theme.header.border);

				// Calculate how many frames fit in the available space
				float timeline_width = header_region_avail.x - label_width;
				int visible_frame_count = static_cast<int>(timeline_width / context.frame_width);

				// Draw frame numbers and grid lines
				for (int i = 0; i <= visible_frame_count; ++i) {
					float x = header_start.x + label_width + i * context.frame_width;
					auto frame_num = context.start_frame_index + i;

					// Grid line
					auto line_color = (frame_num % theme.frame_grid.major_interval == 0) ?
						theme.frame_grid.major_line : theme.frame_grid.minor_line;
					draw_list->AddLine(
						ImVec2(x, header_start.y),
						ImVec2(x, header_end.y),
						line_color);

					// Frame number text (only for major intervals)
					if (frame_num % theme.frame_grid.major_interval == 0) {
						ImVec2 text_pos(x + theme.frame_number.offset_x, header_start.y + theme.frame_number.offset_y);
						ImGui::PushID(static_cast<int>(i));
						ImGui::SetCursorScreenPos(text_pos);
						ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%zu", frame_num);
						ImGui::PopID();
					}
				}
			}

			ImGui::PopStyleVar(2);
		}

		// ===== LAYERS CONTENT SECTION =====
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());

		ImVec2 content_avail = ImGui::GetContentRegionAvail();
		float label_width = 150.0f;
		float timeline_width = content_avail.x - label_width;

		// Create a table-like layout: left panel (labels) and right panel (timeline)
		{
			ImGui::Columns(2, "timeline_layout", false);
			ImGui::SetColumnWidth(0, label_width);

			// ===== LEFT PANEL: LAYER LABELS =====
			{
				ImDrawList* draw_list = ImGui::GetWindowDrawList();

				for (auto&& [index, layer] : context.system->layers() | ::std::views::enumerate) {
					ImVec2 item_start = ImGui::GetCursorScreenPos();

					// Layer background (alternating colors)
					ImU32 bg_color = (index % 2 == 0) ? theme.layer.background_even : theme.layer.background_odd;
					if (context.play_data->current_layer && context.play_data->current_layer == index)
						bg_color = theme.layer.selected_background;

					ImVec2 item_end(item_start.x + label_width, item_start.y + theme.layout.layer_height);
					draw_list->AddRectFilled(item_start, item_end, bg_color);
					draw_list->AddLine(ImVec2(item_end.x, item_start.y), ImVec2(item_end.x, item_end.y), theme.layer.border_right);

					// Layer name text
					ImGui::SetCursorPosX(theme.layer.label_padding_x);
					ImGui::PushID(static_cast<int>(index));
					ImGui::Text("%s", layer.name.c_str());
					ImGui::PopID();

					// Invisible button for selection
					ImGui::SetCursorScreenPos(item_start);
					ImGui::PushID(static_cast<int>(index * 100000));
					ImGui::InvisibleButton("##layer_select", ImVec2(label_width, theme.layout.layer_height));
					if (ImGui::IsItemClicked()) {
						// Update selection - note: context is now non-const
						// This would need to be handled by the caller
					}
					ImGui::PopID();

					// Advance cursor
					ImGui::Dummy(ImVec2(0, theme.layout.layer_height));
				}
			}

			ImGui::NextColumn();

			// ===== RIGHT PANEL: TIMELINE FRAMES =====
			{
				ImDrawList* draw_list = ImGui::GetWindowDrawList();
				ImVec2 timeline_start = ImGui::GetCursorScreenPos();
				int visible_frame_count = static_cast<int>(timeline_width / context.frame_width);
				ImVec2 last_row_end = timeline_start;

				for (auto&& [idx, layer] : context.system->layers() | ::std::views::enumerate) {
					ImVec2 row_start = ImGui::GetCursorScreenPos();
					ImVec2 row_end(row_start.x + timeline_width, row_start.y + theme.layout.layer_height);
					last_row_end = row_end;

					// Row background (alternating colors)
					ImU32 bg_color = (idx % 2 == 0) ? theme.layer.background_even : theme.layer.background_odd;
					draw_list->AddRectFilled(row_start, row_end, bg_color);

					// Draw vertical grid lines
					for (int frame_idx = 0; frame_idx <= visible_frame_count; ++frame_idx) {
						float x = row_start.x + frame_idx * context.frame_width;
						auto frame_num = context.start_frame_index + frame_idx;
						auto line_color = (frame_num % theme.frame_grid.major_interval == 0) ?
							theme.frame_grid.major_line : theme.frame_grid.minor_line;

						draw_list->AddLine(
							ImVec2(x, row_start.y),
							ImVec2(x, row_end.y),
							line_color);
					}

					// Draw keyframes for this layer
					auto keyframe_marker_height = theme.layout.layer_height * theme.keyframe.marker_radius;
					auto keyframe_marker_y = row_start.y + (theme.layout.layer_height - keyframe_marker_height) / 2;

					for (auto&& [keyframe_data, duration_frame, keyframe_index] : layer.timeline._data) {
						auto frame_offset = static_cast<long long>(keyframe_index) - static_cast<long long>(context.start_frame_index);
						if (frame_offset < -static_cast<long long>(duration_frame.hold_frame) ||
							frame_offset >= visible_frame_count)
							continue;

						// Draw duration bar
						auto bar_start_x = row_start.x + std::max(0LL, frame_offset) * context.frame_width;
						auto bar_end_x = row_start.x + std::min(static_cast<long long>(visible_frame_count),
							frame_offset + static_cast<long long>(duration_frame.hold_frame)) * context.frame_width;

						if (bar_start_x < row_start.x + visible_frame_count * context.frame_width &&
							bar_end_x > row_start.x) {
							draw_list->AddRectFilled(
								ImVec2(bar_start_x + 0, keyframe_marker_y),
								ImVec2(bar_end_x - 0, keyframe_marker_y + keyframe_marker_height),
								theme.keyframe.fill);

							draw_list->AddRect(
								ImVec2(bar_start_x + 0, keyframe_marker_y),
								ImVec2(bar_end_x - 0, keyframe_marker_y + keyframe_marker_height),
								theme.keyframe.marker_fill, 0, 0, 1.0f);
						}

						// Draw keyframe marker (diamond)
						if (frame_offset >= -1 && frame_offset <= visible_frame_count) {
							float marker_x = row_start.x + frame_offset * context.frame_width + context.frame_width / 2;
							float marker_size = theme.keyframe.marker_radius;

							ImVec2 marker_points[] = {
								ImVec2(marker_x, keyframe_marker_y - marker_size),
								ImVec2(marker_x + marker_size, keyframe_marker_y),
								ImVec2(marker_x, keyframe_marker_y + marker_size),
								ImVec2(marker_x - marker_size, keyframe_marker_y)
							};

							draw_list->AddConvexPolyFilled(marker_points, 4, theme.keyframe.fill);
							draw_list->AddPolyline(marker_points, 4, theme.keyframe.marker_fill, true, 0);
						}
					}

					// Handle frame interaction (click to select)
					for (int frame_idx = 0; frame_idx < visible_frame_count; ++frame_idx) {
						float frame_x = row_start.x + frame_idx * context.frame_width;
						ImVec2 frame_pos(frame_x, row_start.y);
						ImVec2 frame_size(context.frame_width, theme.layout.layer_height);

						ImGui::PushID(static_cast<int>(idx * 10000 + frame_idx));
						ImGui::SetCursorScreenPos(frame_pos);
						ImGui::InvisibleButton("##frame", frame_size);

						// Frame hover feedback
						if (ImGui::IsItemHovered()) {
							draw_list->AddRectFilled(frame_pos,
								ImVec2(frame_pos.x + frame_size.x, frame_pos.y + frame_size.y),
								theme.interaction.frame_hover_color);
						}

						// Frame selection feedback
						bool is_selected = context.selected_frame.has_value() &&
							context.selected_frame.value() == context.start_frame_index + frame_idx;
						if (is_selected) {
							draw_list->AddRect(frame_pos,
								ImVec2(frame_pos.x + frame_size.x, frame_pos.y + frame_size.y),
								theme.interaction.frame_selected_color, 0.0f, 0, 1.0f);
						}

						if (ImGui::IsItemClicked()) {
							context.selected_frame = context.start_frame_index + frame_idx;
						}

						ImGui::PopID();
					}

					// Advance cursor
					ImGui::Dummy(ImVec2(timeline_width, theme.layout.layer_height));
				}

				// Draw playhead across all layers
				if (context.play_data && context.play_data->current_frame >= context.start_frame_index &&
					context.play_data->current_frame < context.start_frame_index + visible_frame_count) {

					float playhead_x = timeline_start.x +
						(context.play_data->current_frame - context.start_frame_index) * context.frame_width;
					ImVec2 playhead_start(playhead_x, timeline_start.y);
					ImVec2 playhead_end(playhead_x, last_row_end.y);

					draw_list->AddLine(playhead_start, playhead_end,
						theme.playhead.color, theme.playhead.thickness);
				}
			}

			ImGui::Columns(1);
		}

		ImGui::PopStyleVar(2);
	}
}
