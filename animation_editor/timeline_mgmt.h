// Timeline System - Integrated timeline management
// Coordinates pools, UI, and display tree integration

#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include "timeline_data.h"
#include "timeline_layer.h"
#include <cmath>

// Forward declarations
class Stage;
class Clip;
class DisplayObject;

// ============================================================================
// SECTION 1: TIMELINE SYSTEM - Main coordination class
// ============================================================================

class TimelineSystem {
public:
	TimelineSystem()
		: keyframe_pool(std::make_unique<KeyframePool>())
		, timeline_pool(std::make_unique<TimelinePool>())
		, timeline_ui(std::make_unique<TimelineUI>()) {}

	~TimelineSystem() {
		clear();
	}

	// === INITIALIZATION ===

	void initialize(Stage* stage) {
		stage_ref = stage;
	}

	// === TIMELINE POOL ACCESS ===

	Timeline* createTimeline(int frame_count = 120) {
		int timeline_id = timeline_pool->createTimeline(frame_count);
		return timeline_pool->getTimeline(timeline_id);
	}

	Timeline* getTimeline(int timeline_id) {
		return timeline_pool->getTimeline(timeline_id);
	}

	void deleteTimeline(int timeline_id) {
		timeline_pool->deleteTimeline(timeline_id, *keyframe_pool);
	}

	// === KEYFRAME POOL ACCESS ===

	int createKeyframe(int timeline_id, int frame_index) {
		Timeline* timeline = getTimeline(timeline_id);
		if (!timeline) return -1;
		return timeline->createKeyframe(frame_index, *keyframe_pool);
	}

	TimelineKeyframe* getKeyframe(int keyframe_id) {
		return keyframe_pool->getKeyframe(keyframe_id);
	}

	const TimelineKeyframe* getKeyframe(int keyframe_id) const {
		return keyframe_pool->getKeyframe(keyframe_id);
	}

	void deleteKeyframe(int timeline_id, int keyframe_id) {
		Timeline* timeline = getTimeline(timeline_id);
		if (timeline) {
			timeline->deleteKeyframe(keyframe_id, *keyframe_pool);
		}
	}

	// === KEYFRAME CHILD MANAGEMENT ===

	void addChildToKeyframe(int keyframe_id, int display_id) {
		keyframe_pool->addChildToKeyframe(keyframe_id, display_id);
	}

	void removeChildFromKeyframe(int keyframe_id, int display_id) {
		keyframe_pool->removeChildFromKeyframe(keyframe_id, display_id);
	}

	std::vector<int> getKeyframeChildren(int keyframe_id) const {
		const TimelineKeyframe* kf = getKeyframe(keyframe_id);
		return kf ? kf->child_display_ids : std::vector<int>();
	}

	// === LAYER MANAGEMENT ===

	TimelineLayer* createLayer(int clip_id, int timeline_id, const std::string& name) {
		auto layer = std::make_unique<TimelineLayer>(clip_id, timeline_id, name);
		TimelineLayer* layer_ptr = layer.get();
		layers[clip_id] = std::move(layer);
		timeline_ui->addLayer(layer_ptr);
		return layer_ptr;
	}

	TimelineLayer* getLayer(int clip_id) {
		auto it = layers.find(clip_id);
		return it != layers.end() ? it->second.get() : nullptr;
	}

	void deleteLayer(int clip_id) {
		auto it = layers.find(clip_id);
		if (it != layers.end()) {
			TimelineLayer* layer = it->second.get();
			timeline_ui->removeLayer(layer);
			layers.erase(it);
		}
	}

	int getLayerCount() const {
		return (int)layers.size();
	}

	// === TIMELINE STATE ===

	const TimelineState& getState() const { return state; }
	TimelineState& getState() { return state; }

	int getCurrentFrame() const { return state.current_frame; }
	void setCurrentFrame(int frame) {
		state.current_frame = std::max(0, frame);
	}

	bool isPlaying() const { return state.is_playing; }
	void setPlaying(bool playing) { state.is_playing = playing; }

	int getCurrentTimelineId() const { return state.current_timeline_id; }
	void setCurrentTimelineId(int timeline_id) { state.current_timeline_id = timeline_id; }

	// === FRAME QUERYING ===

	int getDisplayKeyframe(int timeline_id, int frame_index) const {
		const Timeline* timeline = timeline_pool->getTimeline(timeline_id);
		return timeline ? timeline->getDisplayKeyframeAtFrame(frame_index, *keyframe_pool) : -1;
	}

	int getKeyframeAtFrame(int timeline_id, int frame_index) const {
		const Timeline* timeline = timeline_pool->getTimeline(timeline_id);
		return timeline ? timeline->getKeyframeAtFrame(frame_index, *keyframe_pool) : -1;
	}

	// === UI RENDERING ===

	TimelineUI* getTimelineUI() { return timeline_ui.get(); }

	void render(ImDrawList* draw_list, ImVec2 panel_pos, ImVec2 panel_size) {
		if (!draw_list) return;

		TimelineUI::RenderContext ctx;
		ctx.draw_list = draw_list;
		ctx.panel_pos = panel_pos;
		ctx.panel_size = panel_size;
		ctx.visible_frame_count = (int)(panel_size.x - TimelineUI::LAYER_LABEL_WIDTH) / (int)TimelineUI::FRAME_WIDTH;

		// Populate keyframe data for rendering
		if (state.current_timeline_id >= 0) {
			Timeline* timeline = getTimeline(state.current_timeline_id);
			if (timeline) {
				ctx.visible_frame_count = std::min(ctx.visible_frame_count, timeline->getFrameCount());

				// Get all keyframes for this timeline
				std::vector<int> keyframes = timeline->getAllKeyframes(*keyframe_pool);
				ctx.visible_keyframes = keyframes;

				// Populate keyframe frame positions and durations
				for (int kf_id : keyframes) {
					const TimelineKeyframe* kf = keyframe_pool->getKeyframe(kf_id);
					if (kf) {
						ctx.keyframe_frames[kf_id] = kf->frame_index;
						ctx.keyframe_durations[kf_id] = kf->duration;
					}
				}
			}
		}

		timeline_ui->render(ctx, state);
	}

	void handleMouseInput(ImVec2 panel_pos, ImVec2 panel_size, ImVec2 mouse_pos) {
		TimelineUI::RenderContext ctx;
		ctx.panel_pos = panel_pos;
		ctx.panel_size = panel_size;
		ctx.visible_frame_count = (int)(panel_size.x - TimelineUI::LAYER_LABEL_WIDTH) / (int)TimelineUI::FRAME_WIDTH;

		timeline_ui->handleMouseInput(ctx, mouse_pos, state);
	}

	// === UPDATE ===

	void update(double delta_time) {
		if (!state.is_playing) return;

		state.last_frame_time += delta_time;
		if (state.last_frame_time >= state.frame_duration_ms) {
			state.last_frame_time -= state.frame_duration_ms;

			// Get current timeline
			Timeline* timeline = getTimeline(state.current_timeline_id);
			if (timeline) {
				state.current_frame = (state.current_frame + 1) % timeline->getFrameCount();
			}
		}
	}

	// === CLEANUP ===

	void clear() {
		layers.clear();
		keyframe_pool->clear();
		timeline_pool->clear();
		timeline_ui->clear();
		state.reset();
		stage_ref = nullptr;
	}

	// === UTILITY ===

	float getTimelineHeight() const {
		return timeline_ui->getTimelineHeight();
	}

	// Set duration (in frames) for a keyframe
	void setKeyframeDuration(int keyframe_id, int duration) {
		TimelineKeyframe* kf = keyframe_pool->getKeyframe(keyframe_id);
		if (kf) {
			kf->duration = std::max(1, duration);
		}
	}

	// Populate a timeline's keyframes from a player's frame durations
	// frames_count: number of frames
	// frame_ms: vector of milliseconds duration per frame (size may be frames_count)
	void populateTimelineFromPlayer(int timeline_id, int frames_count, const std::vector<int>& frame_ms) {
		Timeline* timeline = getTimeline(timeline_id);
		if (!timeline) return;

		for (int i = 0; i < frames_count; ++i) {
			int kf_id = createKeyframe(timeline_id, i);
			int ms = 100;
			if (i >= 0 && i < (int)frame_ms.size()) ms = frame_ms[i];
			// Convert milliseconds to timeline frames using the timeline state's frame duration
			int dur_frames = 1;
			if (state.frame_duration_ms > 0.0f) {
				dur_frames = std::max(1, (int)std::ceil(ms / state.frame_duration_ms));
			}
			setKeyframeDuration(kf_id, dur_frames);
		}
	}

private:
	std::unique_ptr<KeyframePool> keyframe_pool;
	std::unique_ptr<TimelinePool> timeline_pool;
	std::unique_ptr<TimelineUI> timeline_ui;
	std::unordered_map<int, std::unique_ptr<TimelineLayer>> layers;  // clip_id -> TimelineLayer
	TimelineState state;
	Stage* stage_ref = nullptr;
};
