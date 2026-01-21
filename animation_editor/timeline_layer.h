// Timeline UI and Layer Management
// Handles rendering and interaction with timeline panel

#pragma once

#include <imgui.h>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include "timeline_data.h"

// ============================================================================
// SECTION 1: TIMELINE LAYER - Wraps Clip with timeline metadata
// ============================================================================

class TimelineLayer {
public:
	TimelineLayer(int clip_id, int timeline_id, const std::string& name = "Layer")
		: clip_id(clip_id), timeline_id(timeline_id), name(name), frame_count(30) {}

	int getClipId() const { return clip_id; }
	int getTimelineId() const { return timeline_id; }
	const std::string& getName() const { return name; }
	void setName(const std::string& new_name) { name = new_name; }

	// Frame management
	int getFrameCount() const { return frame_count; }
	void setFrameCount(int count) { 
		if (count > 0) frame_count = count;
	}

	// Frame selection
	int getSelectedFrame() const { return selected_frame; }
	void setSelectedFrame(int frame) {
		if (frame >= 0 && frame < frame_count) {
			selected_frame = frame;
		} else if (frame == -1) {
			selected_frame = -1;
		}
	}

	// Frame range selection
	void setSelectedFrameRange(int start, int end) {
		if (start < 0 || end < 0) {
			selected_frame_range_start = -1;
			selected_frame_range_end = -1;
			return;
		}
		int min_frame = std::max(0, std::min(start, end));
		int max_frame = std::min(frame_count - 1, std::max(start, end));
		if (min_frame <= max_frame) {
			selected_frame_range_start = min_frame;
			selected_frame_range_end = max_frame;
		}
	}

	int getSelectedFrameRangeStart() const { return selected_frame_range_start; }
	int getSelectedFrameRangeEnd() const { return selected_frame_range_end; }
	bool hasFrameRangeSelection() const { return selected_frame_range_start >= 0; }

	// Keyframe duration tracking - stores duration for each keyframe
	void setKeyframeDuration(int keyframe_id, int duration) {
		if (duration > 0) {
			keyframe_durations[keyframe_id] = duration;
		}
	}

	int getKeyframeDuration(int keyframe_id) const {
		auto it = keyframe_durations.find(keyframe_id);
		return (it != keyframe_durations.end()) ? it->second : 1;
	}

	// Get total playback duration for a keyframe (child objects play for this many frames)
	int getKeyframePlaybackDuration(int keyframe_id) const {
		return getKeyframeDuration(keyframe_id);
	}

	// UI state
	bool is_visible = true;
	bool is_locked = false;
	bool is_solo = false;
	float height = 40.0f;  // Height in timeline panel

	// Editing state
	bool is_editing = false;

private:
	int clip_id = -1;
	int timeline_id = -1;
	std::string name;
	int frame_count = 30;
	int selected_frame = -1;
	int selected_frame_range_start = -1;
	int selected_frame_range_end = -1;
	std::unordered_map<int, int> keyframe_durations;  // keyframe_id -> duration in frames
};

// ============================================================================
// SECTION 2: TIMELINE UI - Rendering and interaction
// ============================================================================

class TimelineUI {
public:
	TimelineUI() = default;

	// Layer management
	void addLayer(TimelineLayer* layer) {
		if (layer) layers.push_back(layer);
	}

	void removeLayer(TimelineLayer* layer) {
		if (!layer) return;
		auto it = std::find(layers.begin(), layers.end(), layer);
		if (it != layers.end()) layers.erase(it);
	}

	int getLayerCount() const { return (int)layers.size(); }
	TimelineLayer* getLayer(int index) {
		return (index >= 0 && index < (int)layers.size()) ? layers[index] : nullptr;
	}

	// Rendering constants
	static constexpr float LAYER_LABEL_WIDTH = 120.0f;
	static constexpr float FRAME_WIDTH = 15.0f;
	static constexpr float KEYFRAME_MARKER_SIZE = 8.0f;

	struct RenderContext {
		ImDrawList* draw_list = nullptr;
		ImVec2 panel_pos;
		ImVec2 panel_size;
		float scroll_x = 0.0f;           // Horizontal scroll for frame visibility
		float scroll_y = 0.0f;           // Vertical scroll for layer visibility
		int visible_frame_count = 30;    // How many frames visible at current scroll
		int start_frame_index = 0;       // First visible frame
		int selected_frame = -1;         // Currently selected frame
		int hovered_layer_index = -1;
		int hovered_frame = -1;
		int dragging_layer_index = -1;   // Layer being dragged
		int drag_start_frame = -1;       // Start frame of drag operation
		int drag_end_frame = -1;         // End frame of drag operation
		bool is_dragging = false;        // Whether user is dragging frames
		
		// Keyframe visualization data (populated by TimelineSystem)
		std::vector<int> visible_keyframes;  // Keyframe IDs visible in current view
		std::unordered_map<int, int> keyframe_frames;  // keyframe_id -> frame_index
		std::unordered_map<int, int> keyframe_durations;  // keyframe_id -> duration
	};

	// Main render function
	void render(const RenderContext& ctx, const TimelineState& state) {
		if (!ctx.draw_list) return;

		// Draw background
		ctx.draw_list->AddRectFilled(ctx.panel_pos, 
			ImVec2(ctx.panel_pos.x + ctx.panel_size.x, ctx.panel_pos.y + ctx.panel_size.y),
			IM_COL32(30, 30, 30, 255));

		// Draw frame header
		renderFrameHeader(ctx, state);

		// Draw layers
		ImVec2 layer_start = ImVec2(ctx.panel_pos.x, ctx.panel_pos.y + 20.0f);
		float y_offset = layer_start.y - ctx.scroll_y;
		
		for (size_t i = 0; i < layers.size(); ++i) {
			if (y_offset + layers[i]->height < ctx.panel_pos.y) {
				y_offset += layers[i]->height;
				continue;
			}
			if (y_offset > ctx.panel_pos.y + ctx.panel_size.y) break;

			renderLayer(ctx, *layers[i], (int)i, ImVec2(layer_start.x, y_offset), state);
			y_offset += layers[i]->height;
		}

		// Draw playhead
		if (state.current_frame >= ctx.start_frame_index && 
			state.current_frame < ctx.start_frame_index + ctx.visible_frame_count) {
			renderPlayhead(ctx, state);
		}
	}

	// Interaction
	void handleMouseInput(RenderContext& ctx, ImVec2 mouse_pos, TimelineState& state) {
		// Check if mouse is over timeline
		if (mouse_pos.x < ctx.panel_pos.x || mouse_pos.x > ctx.panel_pos.x + ctx.panel_size.x ||
			mouse_pos.y < ctx.panel_pos.y || mouse_pos.y > ctx.panel_pos.y + ctx.panel_size.y) {
			ctx.is_dragging = false;
			return;
		}

		float label_right = ctx.panel_pos.x + LAYER_LABEL_WIDTH;
		float y_offset = ctx.panel_pos.y + 20.0f - ctx.scroll_y;

		// Find hovered layer
		int hovered_layer = -1;
		for (size_t i = 0; i < layers.size(); ++i) {
			if (mouse_pos.y >= y_offset && mouse_pos.y < y_offset + layers[i]->height) {
				hovered_layer = (int)i;
				break;
			}
			y_offset += layers[i]->height;
		}

		// Layer label selection
		if (mouse_pos.x < label_right) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_layer >= 0) {
				selected_layer_index = hovered_layer;
				layers[hovered_layer]->is_editing = true;
			}
			ctx.is_dragging = false;
			return;
		}

		// Frame interaction in timeline area
		if (hovered_layer >= 0 && mouse_pos.x > label_right) {
			int frame_offset = (int)((mouse_pos.x - label_right) / FRAME_WIDTH);
			int clicked_frame = ctx.start_frame_index + frame_offset;

			// Handle drag start
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				ctx.dragging_layer_index = hovered_layer;
				ctx.drag_start_frame = clicked_frame;
				ctx.is_dragging = true;
				layers[hovered_layer]->setSelectedFrame(clicked_frame);
				state.current_frame = clicked_frame;
			}
			// Handle drag continuation
			else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ctx.is_dragging && 
					 ctx.dragging_layer_index == hovered_layer) {
				ctx.drag_end_frame = clicked_frame;
				layers[hovered_layer]->setSelectedFrameRange(ctx.drag_start_frame, ctx.drag_end_frame);
			}
			// Handle drag release
			else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ctx.is_dragging) {
				ctx.is_dragging = false;
			}
		}
	}

	// Overload for const RenderContext (for backward compatibility)
	void handleMouseInput(const RenderContext& ctx_const, ImVec2 mouse_pos, TimelineState& state) {
		RenderContext ctx = ctx_const;
		handleMouseInput(ctx, mouse_pos, state);
	}

	int getSelectedLayerIndex() const { return selected_layer_index; }
	void setSelectedLayerIndex(int idx) { selected_layer_index = idx; }

	// Utility
	float getTimelineHeight() const {
		float height = 20.0f;  // Frame header
		for (auto layer : layers) {
			height += layer->height;
		}
		return height;
	}

	void clear() {
		layers.clear();
		selected_layer_index = -1;
	}

private:
	std::vector<TimelineLayer*> layers;
	int selected_layer_index = -1;

	void renderFrameHeader(const RenderContext& ctx, const TimelineState& state) {
		ImVec2 header_start = ImVec2(ctx.panel_pos.x + LAYER_LABEL_WIDTH, ctx.panel_pos.y);
		ImVec2 header_end = ImVec2(ctx.panel_pos.x + ctx.panel_size.x, ctx.panel_pos.y + 20.0f);

		// Header background
		ctx.draw_list->AddRectFilled(header_start, header_end, IM_COL32(40, 40, 40, 255));
		ctx.draw_list->AddRect(header_start, header_end, IM_COL32(80, 80, 80, 255));

		// Draw frame numbers
		for (int i = 0; i < ctx.visible_frame_count; ++i) {
			int frame = ctx.start_frame_index + i;
			float x = header_start.x + i * FRAME_WIDTH;
			if (frame % 5 == 0) {  // Every 5 frames
				ImGui::SetCursorScreenPos(ImVec2(x + 2, header_start.y + 3));
				ImGui::TextUnformatted(std::to_string(frame).c_str());
			}
		}
	}

	void renderLayer(const RenderContext& ctx, TimelineLayer& layer, int layer_idx, 
		ImVec2 layer_pos, const TimelineState& state) {
		float layer_height = layer.height;

		// Layer background (alternating colors)
		ImU32 bg_color = (layer_idx % 2 == 0) ? IM_COL32(45, 45, 45, 255) : IM_COL32(50, 50, 50, 255);
		if (selected_layer_index == layer_idx) {
			bg_color = IM_COL32(60, 80, 100, 255);
		}

		ctx.draw_list->AddRectFilled(layer_pos,
			ImVec2(layer_pos.x + ctx.panel_size.x, layer_pos.y + layer_height),
			bg_color);

		// Layer label
		ImVec2 label_pos = ImVec2(layer_pos.x + 5, layer_pos.y + layer_height / 2 - 7);
		std::string label = layer.getName() + " (ID: " + std::to_string(layer.getClipId()) + ")";
		ctx.draw_list->AddText(label_pos, IM_COL32(200, 200, 200, 255), label.c_str());

		// Timeline area
		ImVec2 timeline_start = ImVec2(layer_pos.x + LAYER_LABEL_WIDTH, layer_pos.y);
		ImVec2 timeline_end = ImVec2(layer_pos.x + ctx.panel_size.x, layer_pos.y + layer_height);

		// Draw frames (grid)
		for (int i = 0; i < ctx.visible_frame_count; ++i) {
			float x = timeline_start.x + i * FRAME_WIDTH;
			ImU32 line_color = (i % 5 == 0) ? IM_COL32(80, 80, 80, 255) : IM_COL32(60, 60, 60, 255);
			ctx.draw_list->AddLine(ImVec2(x, timeline_start.y), ImVec2(x, timeline_end.y), line_color);
		}

		// Draw frame selections
		renderFrameSelections(ctx, layer, timeline_start, layer_height, ctx.start_frame_index);

		// Right border
		ctx.draw_list->AddLine(timeline_end, ImVec2(timeline_end.x, timeline_start.y), IM_COL32(80, 80, 80, 255));

		// Bottom border
		ctx.draw_list->AddLine(ImVec2(timeline_start.x, timeline_end.y), timeline_end, IM_COL32(80, 80, 80, 255));

		// Render keyframes (placeholder - will be enhanced in TimelineSystem)
		renderLayerKeyframes(ctx, layer, timeline_start, layer_height, state);
	}

	void renderFrameSelections(const RenderContext& ctx, TimelineLayer& layer, 
		ImVec2 timeline_start, float layer_height, int start_frame_index) {
		// Draw selected single frame
		if (layer.getSelectedFrame() >= 0) {
			int sel_frame = layer.getSelectedFrame();
			int frame_offset = sel_frame - start_frame_index;
			if (frame_offset >= 0 && frame_offset < ctx.visible_frame_count) {
				float x = timeline_start.x + frame_offset * FRAME_WIDTH;
				ctx.draw_list->AddRectFilled(
					ImVec2(x + 1, timeline_start.y + 1),
					ImVec2(x + FRAME_WIDTH - 1, timeline_start.y + layer_height - 1),
					IM_COL32(100, 200, 255, 200));
			}
		}

		// Draw selected frame range
		if (layer.hasFrameRangeSelection()) {
			int range_start = layer.getSelectedFrameRangeStart();
			int range_end = layer.getSelectedFrameRangeEnd();
			for (int f = range_start; f <= range_end; ++f) {
				int frame_offset = f - start_frame_index;
				if (frame_offset >= 0 && frame_offset < ctx.visible_frame_count) {
					float x = timeline_start.x + frame_offset * FRAME_WIDTH;
					ctx.draw_list->AddRectFilled(
						ImVec2(x + 1, timeline_start.y + 1),
						ImVec2(x + FRAME_WIDTH - 1, timeline_start.y + layer_height - 1),
						IM_COL32(150, 200, 100, 200));
				}
			}
		}
	}

	void renderLayerKeyframes(const RenderContext& ctx, TimelineLayer& layer, 
		ImVec2 timeline_start, float layer_height, const TimelineState& state) {
		// Render keyframe duration visualization
		// Shows which frames have keyframes and how long they persist
		
		int timeline_id = layer.getTimelineId();
		if (timeline_id < 0) return;
		
		// Render keyframe markers from the provided data
		const float KEYFRAME_MARKER_HEIGHT = layer_height * 0.5f;
		const float KEYFRAME_MARKER_Y = timeline_start.y + (layer_height - KEYFRAME_MARKER_HEIGHT) / 2;
		
		// Draw duration bars for each keyframe
		for (int kf_id : ctx.visible_keyframes) {
			auto frame_it = ctx.keyframe_frames.find(kf_id);
			auto duration_it = ctx.keyframe_durations.find(kf_id);
			
			if (frame_it == ctx.keyframe_frames.end() || duration_it == ctx.keyframe_durations.end()) {
				continue;
			}
			
			int frame_index = frame_it->second;
			int duration = duration_it->second;
			
			// Check if this keyframe is visible
			int frame_offset = frame_index - ctx.start_frame_index;
			if (frame_offset < -duration || frame_offset >= ctx.visible_frame_count) {
				continue;
			}
			
			// Draw duration bar (shows how long this keyframe persists)
			float bar_start_x = timeline_start.x + std::max(0, frame_offset) * FRAME_WIDTH;
			float bar_end_x = timeline_start.x + std::min(ctx.visible_frame_count, frame_offset + duration) * FRAME_WIDTH;
			
			if (bar_start_x < timeline_start.x + ctx.visible_frame_count * FRAME_WIDTH && bar_end_x > timeline_start.x) {
				// Draw duration bar in semi-transparent color
				ctx.draw_list->AddRectFilled(
					ImVec2(bar_start_x + 1, KEYFRAME_MARKER_Y),
					ImVec2(bar_end_x - 1, KEYFRAME_MARKER_Y + KEYFRAME_MARKER_HEIGHT),
					IM_COL32(100, 150, 200, 150));
				
				// Draw border around the bar
				ctx.draw_list->AddRect(
					ImVec2(bar_start_x + 1, KEYFRAME_MARKER_Y),
					ImVec2(bar_end_x - 1, KEYFRAME_MARKER_Y + KEYFRAME_MARKER_HEIGHT),
					IM_COL32(100, 150, 255, 255), 0.0f, 15, 1.0f);
			}
			
			// Draw keyframe marker (diamond shape at start frame)
			if (frame_offset >= -1 && frame_offset <= ctx.visible_frame_count) {
				float marker_x = timeline_start.x + frame_offset * FRAME_WIDTH + FRAME_WIDTH / 2;
				float marker_size = KEYFRAME_MARKER_SIZE;
				
				ImVec2 marker_points[] = {
					ImVec2(marker_x, KEYFRAME_MARKER_Y - marker_size),
					ImVec2(marker_x + marker_size, KEYFRAME_MARKER_Y),
					ImVec2(marker_x, KEYFRAME_MARKER_Y + marker_size),
					ImVec2(marker_x - marker_size, KEYFRAME_MARKER_Y)
				};
				
				ctx.draw_list->AddConvexPolyFilled(marker_points, 4, IM_COL32(255, 200, 100, 255));
				ctx.draw_list->AddPolyline(marker_points, 4, IM_COL32(255, 150, 0, 255), true, 1.5f);
			}
		}
	}

	void renderPlayhead(const RenderContext& ctx, const TimelineState& state) {
		float frame_offset = (state.current_frame - ctx.start_frame_index) * FRAME_WIDTH;
		float x = ctx.panel_pos.x + LAYER_LABEL_WIDTH + frame_offset;
		
		ctx.draw_list->AddLine(ImVec2(x, ctx.panel_pos.y), 
			ImVec2(x, ctx.panel_pos.y + ctx.panel_size.y), IM_COL32(255, 100, 100, 255), 2.0f);
	}
};
