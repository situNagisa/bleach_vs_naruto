// Timeline System - Data Structures and Pools
// Implements keyframe pool and timeline data management
// All data is stored in pools, not on individual frames

#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <algorithm>
#include <chrono>

// ============================================================================
// SECTION 1: KEYFRAME DATA STRUCTURE
// ============================================================================

struct TimelineKeyframe {
	int id = -1;                           // Keyframe unique identifier
	int frame_index = 0;                   // Frame index where this keyframe is placed
	std::vector<int> child_display_ids;    // DisplayObject IDs contained in this keyframe

	TimelineKeyframe() = default;
	TimelineKeyframe(int id, int frame_idx)
		: id(id), frame_index(frame_idx) {}
};

// ============================================================================
// SECTION 1B: TWEEN INTERFACE - For frame interpolation
// ============================================================================

class FrameTween {
public:
	virtual ~FrameTween() = default;

	// Evaluate tween at local frame position
	// local_frame: frame within span (0 to span_length-1)
	// span_length: total frames in this span
	// Returns interpolation factor [0.0, 1.0]
	virtual float evaluate(int local_frame, int span_length) const = 0;

	// Clone for serialization
	virtual std::unique_ptr<FrameTween> clone() const = 0;
};

// Linear tween (no interpolation)
class LinearTween : public FrameTween {
public:
	float evaluate(int local_frame, int span_length) const override {
		if (span_length <= 1) return 1.0f;
		return static_cast<float>(local_frame) / (span_length - 1);
	}

	std::unique_ptr<FrameTween> clone() const override {
		return std::make_unique<LinearTween>();
	}
};

// Ease-in tween (cubic)
class EaseInTween : public FrameTween {
public:
	float evaluate(int local_frame, int span_length) const override {
		if (span_length <= 1) return 1.0f;
		float t = static_cast<float>(local_frame) / (span_length - 1);
		return t * t * t;
	}

	std::unique_ptr<FrameTween> clone() const override {
		return std::make_unique<EaseInTween>();
	}
};

// Ease-out tween (cubic)
class EaseOutTween : public FrameTween {
public:
	float evaluate(int local_frame, int span_length) const override {
		if (span_length <= 1) return 1.0f;
		float t = static_cast<float>(local_frame) / (span_length - 1);
		t = 1.0f - t;
		return 1.0f - (t * t * t);
	}

	std::unique_ptr<FrameTween> clone() const override {
		return std::make_unique<EaseOutTween>();
	}
};

// ============================================================================
// SECTION 2: KEYFRAME POOL - Centralized keyframe storage
// ============================================================================

class KeyframePool {
public:
	KeyframePool() = default;
	~KeyframePool() = default;

	// Create a new keyframe
	int createKeyframe(int frame_index) {
		int id = next_keyframe_id++;
		TimelineKeyframe kf(id, frame_index);
		keyframes[id] = kf;
		return id;
	}

	// Get keyframe by ID
	TimelineKeyframe* getKeyframe(int id) {
		auto it = keyframes.find(id);
		return it != keyframes.end() ? &it->second : nullptr;
	}

	const TimelineKeyframe* getKeyframe(int id) const {
		auto it = keyframes.find(id);
		return it != keyframes.end() ? &it->second : nullptr;
	}

	// Get all keyframes for a timeline
	std::vector<int> getKeyframesInTimeline(int timeline_id) const {
		auto it = timeline_keyframes.find(timeline_id);
		return it != timeline_keyframes.end() ? it->second : std::vector<int>();
	}

	// Add keyframe to timeline
	void addKeyframeToTimeline(int timeline_id, int keyframe_id) {
		timeline_keyframes[timeline_id].push_back(keyframe_id);
		// Sort by frame index
		sortTimelineKeyframes(timeline_id);
	}

	// Remove keyframe from timeline
	void removeKeyframeFromTimeline(int timeline_id, int keyframe_id) {
		auto it = timeline_keyframes.find(timeline_id);
		if (it != timeline_keyframes.end()) {
			auto& kfs = it->second;
			kfs.erase(std::remove(kfs.begin(), kfs.end(), keyframe_id), kfs.end());
		}
		keyframes.erase(keyframe_id);
	}

	// Delete keyframe completely
	void deleteKeyframe(int keyframe_id) {
		keyframes.erase(keyframe_id);
	}

	// Get keyframe at frame index
	int getKeyframeAtFrame(int timeline_id, int frame_index) const {
		auto it = timeline_keyframes.find(timeline_id);
		if (it != timeline_keyframes.end()) {
			for (int kf_id : it->second) {
				auto kf_it = keyframes.find(kf_id);
				if (kf_it != keyframes.end() && kf_it->second.frame_index == frame_index) {
					return kf_id;
				}
			}
		}
		return -1;
	}

	// Get keyframe at or before frame index
	int getKeyframeAtOrBeforeFrame(int timeline_id, int frame_index) const {
		auto it = timeline_keyframes.find(timeline_id);
		if (it == timeline_keyframes.end()) return -1;

		int result = -1;
		int closest_frame = -1;
		for (int kf_id : it->second) {
			auto kf_it = keyframes.find(kf_id);
			if (kf_it != keyframes.end()) {
				int kf_frame = kf_it->second.frame_index;
				if (kf_frame <= frame_index && kf_frame > closest_frame) {
					closest_frame = kf_frame;
					result = kf_id;
				}
			}
		}
		return result;
	}

	// Add child to keyframe
	void addChildToKeyframe(int keyframe_id, int child_id) {
		TimelineKeyframe* kf = getKeyframe(keyframe_id);
		if (kf) {
			auto& children = kf->child_display_ids;
			if (std::find(children.begin(), children.end(), child_id) == children.end()) {
				children.push_back(child_id);
			}
		}
	}

	// Remove child from keyframe
	void removeChildFromKeyframe(int keyframe_id, int child_id) {
		TimelineKeyframe* kf = getKeyframe(keyframe_id);
		if (kf) {
			auto& children = kf->child_display_ids;
			children.erase(std::remove(children.begin(), children.end(), child_id), children.end());
		}
	}

	// Clear all data
	void clear() {
		keyframes.clear();
		timeline_keyframes.clear();
		next_keyframe_id = 0;
	}

private:
	std::unordered_map<int, TimelineKeyframe> keyframes;
	std::unordered_map<int, std::vector<int>> timeline_keyframes;  // timeline_id -> [keyframe_ids]
	int next_keyframe_id = 0;

	void sortTimelineKeyframes(int timeline_id) {
		auto it = timeline_keyframes.find(timeline_id);
		if (it != timeline_keyframes.end()) {
			auto& kfs = it->second;
			std::sort(kfs.begin(), kfs.end(), [this](int a, int b) {
				auto kf_a = keyframes.find(a);
				auto kf_b = keyframes.find(b);
				if (kf_a == keyframes.end() || kf_b == keyframes.end()) return false;
				return kf_a->second.frame_index < kf_b->second.frame_index;
			});
		}
	}
};

// ============================================================================
// SECTION 3: TIMELINE - Layer-specific timeline
// ============================================================================

class Timeline {
public:
	Timeline(int id)
		: id(id) {}

	int getId() const { return id; }
	int getFrameCount() const { return spans.empty() ? 0 : spans.back().end; }
	void setFrameCount(int count) { 
		if (spans.empty()) return;
		spans.back().end = ::std::max(spans.back().start + 1, count);
	}

	// FrameSpan - represents an interval [start, end) with a keyframe at start
	struct FrameSpan {
		int start = 0;
		int end = 0; // exclusive
		int keyframe_id = -1;
		std::unique_ptr<FrameTween> tween;  // Optional tween for interpolation

		FrameSpan() = default;
		FrameSpan(const FrameSpan& other)
			: start(other.start), end(other.end), keyframe_id(other.keyframe_id) {
			if (other.tween) {
				tween = other.tween->clone();
			}
		}

		FrameSpan& operator=(const FrameSpan& other) {
			start = other.start;
			end = other.end;
			keyframe_id = other.keyframe_id;
			if (other.tween) {
				tween = other.tween->clone();
			} else {
				tween = nullptr;
			}
			return *this;
		}

		FrameSpan(FrameSpan&& other) noexcept
			: start(other.start), end(other.end), keyframe_id(other.keyframe_id),
			  tween(std::move(other.tween)) {}

		FrameSpan& operator=(FrameSpan&& other) noexcept {
			start = other.start;
			end = other.end;
			keyframe_id = other.keyframe_id;
			tween = std::move(other.tween);
			return *this;
		}

		// Set tween for this span
		void setTween(std::unique_ptr<FrameTween> new_tween) {
			tween = std::move(new_tween);
		}

		// Get interpolation factor for a frame within this span
		float getTweenValue(int frame_index) const {
			if (frame_index < start || frame_index >= end) return 0.0f;
			int local_frame = frame_index - start;
			int span_length = end - start;

			if (tween) {
				return tween->evaluate(local_frame, span_length);
			}
			// Default: step at keyframe start
			return (local_frame == 0) ? 1.0f : 0.0f;
		}
	};

	// Create a keyframe at frame_index and ensure FrameSpan semantics
	int createKeyframe(int frame_index, KeyframePool& pool) {
		assert(frame_index >= 0);

		// Create keyframe in the pool
		int kf_id = pool.createKeyframe(frame_index);

		// Find span that contains frame_index
		for (size_t i = 0; i < spans.size(); ++i) {
			FrameSpan s = spans[i];
			if (frame_index == s.start) {
				// Replace existing keyframe at start
				int old_kf = spans[i].keyframe_id;
				spans[i].keyframe_id = kf_id;
				// remove old keyframe from pool
				pool.deleteKeyframe(old_kf);
				return kf_id;
			} else if (frame_index > s.start && frame_index < s.end) {
				// Split span into [start, frame_index) and [frame_index, end)
				int old_end = s.end;
				int old_kf = s.keyframe_id;
				spans[i].end = frame_index; // left span keeps old keyframe
				FrameSpan right;
				right.start = frame_index;
				right.end = old_end;
				right.keyframe_id = kf_id; // new keyframe for right span
				spans.insert(spans.begin() + i + 1, right);
				return kf_id;
			}
		}

		// No spanning span contains frame_index: insert new span of length 1 at position
		FrameSpan span;
		span.start = frame_index;
		span.end = frame_index + 1;
		span.keyframe_id = kf_id;

		// Insert maintaining order
		auto it = spans.begin();
		while (it != spans.end() && it->start < frame_index) ++it;
		spans.insert(it, span);
		return kf_id;
	}

	void deleteKeyframe(int keyframe_id, KeyframePool& pool) {
		// remove spans that reference this keyframe
		auto it = spans.begin();
		while (it != spans.end()) {
			if (it->keyframe_id == keyframe_id) {
				// remove span and delete keyframe from pool
				pool.deleteKeyframe(keyframe_id);
				it = spans.erase(it);
			} else {
				++it;
			}
		}
	}

	// Get keyframe at exact frame index (i.e., span start)
	int getKeyframeAtFrame(int frame_index, const KeyframePool& pool) const {
		for (const auto& s : spans) {
			if (s.start == frame_index) return s.keyframe_id;
		}
		return -1;
	}

	// Get keyframe at or before frame index (closest start <= frame_index)
	int getKeyframeAtOrBeforeFrame(int frame_index, const KeyframePool& pool) const {
		int result = -1;
		int best_start = -1;
		for (const auto& s : spans) {
			if (s.start <= frame_index && s.start > best_start) {
				best_start = s.start;
				result = s.keyframe_id;
			}
		}
		return result;
	}

	std::vector<int> getAllKeyframes(const KeyframePool& pool) const {
		std::vector<int> out;
		for (const auto& s : spans) out.push_back(s.keyframe_id);
		return out;
	}

	// For a given frame index, return the keyframe that should be displayed (span covering frame)
	int getDisplayKeyframeAtFrame(int frame_index, const KeyframePool& pool) const {
		for (const auto& s : spans) {
			if (frame_index >= s.start && frame_index < s.end) return s.keyframe_id;
		}
		return -1;
	}

	// Get FrameSpan covering frame_index
	const FrameSpan* getFrameSpanAt(int frame_index) const {
		for (const auto& s : spans) {
			if (frame_index >= s.start && frame_index < s.end) return &s;
		}
		return nullptr;
	}

	FrameSpan* getFrameSpanAt(int frame_index) {
		for (auto& s : spans) {
			if (frame_index >= s.start && frame_index < s.end) return &s;
		}
		return nullptr;
	}

	// Get all spans (for serialization/inspection)
	const std::vector<FrameSpan>& getSpans() const { return spans; }
	std::vector<FrameSpan>& getMutableSpans() { return spans; }

	// Shift spans starting at or after frame_index by offset (positive or negative), used for insert/delete frames
	void shiftSpansAfter(int frame_index, int offset) {
		for (auto& s : spans) {
			if (s.start >= frame_index) {
				s.start += offset;
				s.end += offset;
			}
		}
		// Remove any invalid spans where start >= end
		spans.erase(std::remove_if(spans.begin(), spans.end(), [](const FrameSpan& s) { return s.start >= s.end; }), spans.end());
	}

	// Truncate or remove spans overlapping [f, f+n)
	void removeFramesRange(int f, int n, KeyframePool& pool) {
		int rstart = f;
		int rend = f + n; // exclusive
		std::vector<FrameSpan> new_spans;
		for (auto& s : spans) {
			if (s.end <= rstart) {
				// entirely before, keep
				new_spans.push_back(s);
			} else if (s.start >= rend) {
				// entirely after, will be shifted later
				FrameSpan s2 = s;
				s2.start -= n;
				s2.end -= n;
				new_spans.push_back(s2);
			} else {
				// overlap: may be truncated
				if (s.start < rstart && s.end > rend) {
					// span surrounds deletion range: split into left [start,rstart) and right [rend,end) shifted
					FrameSpan left = s;
					left.end = rstart;
					FrameSpan right = s;
					right.start = rend - n; // after shift
					right.end = s.end - n;
					// left keeps original keyframe, right needs a new keyframe? According to semantics, the right span must start with a keyframe at rend (which didn't exist) ¡ª so we drop right's keyframe
					// We'll remove right span (more conservative) to avoid creating ambiguous keyframe
					new_spans.push_back(left);
					// delete original keyframe for right part
					pool.deleteKeyframe(s.keyframe_id);
				} else if (s.start < rstart) {
					// left portion remains
					FrameSpan left = s;
					left.end = rstart;
					new_spans.push_back(left);
					// delete keyframe if its start was within deletion? not needed
				} else if (s.end > rend) {
					// right portion remains shifted
					FrameSpan right = s;
					right.start = s.start - n;
					right.end = s.end - n;
					// keyframe at original s.start is removed because it fell inside deletion
					pool.deleteKeyframe(s.keyframe_id);
					// We cannot create a new starting keyframe here; skip adding right span
				}
			}
		}
		spans.swap(new_spans);
		// Remove any invalid spans
		spans.erase(std::remove_if(spans.begin(), spans.end(), [](const FrameSpan& s) { return s.start >= s.end; }), spans.end());
	}

private:
	int id = -1;
	int frame_count = 120;
	std::vector<FrameSpan> spans; // ordered by start
};

// ============================================================================
// SECTION 4: TIMELINE POOL - Centralized timeline storage
// ============================================================================

class TimelinePool {
public:
	TimelinePool() = default;
	~TimelinePool() { clear(); }

	// Create timeline for a layer
	int createTimeline() {
		int id = next_timeline_id++;
		timelines[id] = std::make_unique<Timeline>(id);
		return id;
	}

	// Get timeline
	Timeline* getTimeline(int id) {
		auto it = timelines.find(id);
		return it != timelines.end() ? it->second.get() : nullptr;
	}

	const Timeline* getTimeline(int id) const {
		auto it = timelines.find(id);
		return it != timelines.end() ? it->second.get() : nullptr;
	}

	// Delete timeline
	void deleteTimeline(int id, KeyframePool& keyframe_pool) {
		auto it = timelines.find(id);
		if (it != timelines.end()) {
			// Clean up keyframes associated with this timeline
			std::vector<int> keyframes = keyframe_pool.getKeyframesInTimeline(id);
			for (int kf_id : keyframes) {
				keyframe_pool.deleteKeyframe(kf_id);
			}
			timelines.erase(it);
		}
	}

	// Clear all timelines
	void clear() {
		timelines.clear();
		next_timeline_id = 0;
	}

private:
	std::unordered_map<int, std::unique_ptr<Timeline>> timelines;
	int next_timeline_id = 0;
};

// ============================================================================
// SECTION 5: LAYER - Wraps Timeline with editing metadata
// ============================================================================

class Layer {
public:
	Layer(int layer_id, int timeline_id, const std::string& name = "Layer")
		: id(layer_id), timeline_id(timeline_id), name(name) {}

	int getId() const { return id; }
	int getTimelineId() const { return timeline_id; }

	const std::string& getName() const { return name; }
	void setName(const std::string& new_name) { name = new_name; }

	// Editing properties (do not affect data evaluation)
	bool isVisible() const { return visible; }
	void setVisible(bool v) { visible = v; }

	bool isLocked() const { return locked; }
	void setLocked(bool l) { locked = l; }

	bool isSolo() const { return solo; }
	void setSolo(bool s) { solo = s; }

	int getOrder() const { return layer_order; }
	void setOrder(int order) { layer_order = order; }

	// Frame selection state (for editor UI)
	int getSelectedFrame() const { return selected_frame; }
	void setSelectedFrame(int frame) { selected_frame = frame; }

	bool hasFrameRangeSelection() const { 
		return selection_range_start >= 0 && selection_range_end >= 0;
	}

	int getSelectionRangeStart() const { return selection_range_start; }
	int getSelectionRangeEnd() const { return selection_range_end; }

	void setFrameRangeSelection(int start, int end) {
		if (start < 0 || end < 0) {
			selection_range_start = -1;
			selection_range_end = -1;
			return;
		}
		selection_range_start = std::min(start, end);
		selection_range_end = std::max(start, end);
	}

private:
	int id = -1;
	int timeline_id = -1;
	std::string name;
	int layer_order = 0;
	bool visible = true;
	bool locked = false;
	bool solo = false;
	int selected_frame = -1;
	int selection_range_start = -1;
	int selection_range_end = -1;
};

// ============================================================================
// SECTION 6: TIMELINE ASSET - Immutable data for serialization
// ============================================================================

class TimelineAsset {
public:
	struct LayerData {
		int id = -1;
		int timeline_id = -1;
		std::string name;
		int layer_order = 0;
		bool visible = true;
		bool locked = false;
		bool solo = false;
	};

	TimelineAsset() = default;
	TimelineAsset(int asset_id, int frame_count, int fps)
		: id(asset_id), frame_count(frame_count), fps(fps) {}

	int getId() const { return id; }
	int getFrameCount() const { return frame_count; }
	void setFrameCount(int count) { frame_count = std::max(1, count); }

	int getFPS() const { return fps; }
	void setFPS(int new_fps) { fps = std::max(1, new_fps); }

	// Layer data
	int getLayerCount() const { return (int)layers.size(); }

	const LayerData* getLayerData(int index) const {
		return (index >= 0 && index < (int)layers.size()) ? &layers[index] : nullptr;
	}

	// Find layer by ID
	const LayerData* getLayerDataById(int layer_id) const {
		for (const auto& layer : layers) {
			if (layer.id == layer_id) return &layer;
		}
		return nullptr;
	}

	// Timeline data (immutable from outside)
	const Timeline* getTimeline(int timeline_id) const {
		auto it = timelines.find(timeline_id);
		return it != timelines.end() ? it->second.get() : nullptr;
	}

	// Keyframe data (immutable from outside)
	const TimelineKeyframe* getKeyframe(int keyframe_id) const {
		return keyframe_pool.getKeyframe(keyframe_id);
	}

	// Keyframe children
	std::vector<int> getKeyframeChildren(int keyframe_id) const {
		const TimelineKeyframe* kf = getKeyframe(keyframe_id);
		return kf ? kf->child_display_ids : std::vector<int>();
	}

	// === FRAME EVALUATION SEMANTICS ===

	// Get the keyframe active at frame_index for a given timeline
	// Returns the keyframe ID that covers this frame (or -1 if none)
	int getKeyframeAtFrame(int timeline_id, int frame_index) const {
		const Timeline* timeline = getTimeline(timeline_id);
		if (!timeline) return -1;
		return timeline->getDisplayKeyframeAtFrame(frame_index, keyframe_pool);
	}

	// Get keyframe at or before frame (closest start <= frame_index)
	int getKeyframeAtOrBeforeFrame(int timeline_id, int frame_index) const {
		const Timeline* timeline = getTimeline(timeline_id);
		if (!timeline) return -1;
		return timeline->getKeyframeAtOrBeforeFrame(frame_index, keyframe_pool);
	}

	// Get the FrameSpan covering a frame
	const Timeline::FrameSpan* getFrameSpanAt(int timeline_id, int frame_index) const {
		const Timeline* timeline = getTimeline(timeline_id);
		if (!timeline) return nullptr;
		return timeline->getFrameSpanAt(frame_index);
	}

	// === INTERNAL EDITING INTERFACE ===
	// (These methods should only be called during editing/construction)

	int createLayer(const std::string& name) {
		LayerData layer;
		layer.id = next_layer_id++;
		layer.timeline_id = -1;  // Will be assigned later
		layer.name = name;
		layer.layer_order = (int)layers.size();
		layers.push_back(layer);
		return layer.id;
	}

	void updateLayerData(int layer_id, const LayerData& data) {
		for (auto& layer : layers) {
			if (layer.id == layer_id) {
				layer = data;
				return;
			}
		}
	}

	void deleteLayer(int layer_id) {
		auto it = std::find_if(layers.begin(), layers.end(),
			[layer_id](const LayerData& l) { return l.id == layer_id; });
		if (it != layers.end()) {
			layers.erase(it);
		}
	}

	// Mutable access for internal construction (should be used carefully)
	std::vector<LayerData>& getMutableLayers() { return layers; }
	std::unordered_map<int, std::unique_ptr<Timeline>>& getMutableTimelines() { return timelines; }
	KeyframePool& getMutableKeyframePool() { return keyframe_pool; }

	// Timeline creation
	int createTimeline() {
		int id = next_timeline_id++;
		timelines[id] = std::make_unique<Timeline>(id);
		return id;
	}

	Timeline* getTimelineForEdit(int timeline_id) {
		auto it = timelines.find(timeline_id);
		return it != timelines.end() ? it->second.get() : nullptr;
	}

	void deleteTimeline(int timeline_id) {
		auto it = timelines.find(timeline_id);
		if (it != timelines.end()) {
			// Clean up keyframes associated with this timeline
			std::vector<int> keyframes = keyframe_pool.getKeyframesInTimeline(timeline_id);
			for (int kf_id : keyframes) {
				keyframe_pool.deleteKeyframe(kf_id);
			}
			timelines.erase(it);
		}
	}

	// === FRAME EDITING OPERATIONS ===

	// Insert a keyframe at frame_index in timeline
	// If frame already has a keyframe, replace it
	// If frame is within a span, split the span
	// Returns keyframe ID, or -1 on error
	int insertKeyframeAtFrame(int timeline_id, int frame_index) {
		Timeline* timeline = getTimelineForEdit(timeline_id);
		if (!timeline || frame_index < 0 || frame_index >= frame_count) return -1;

		return timeline->createKeyframe(frame_index, keyframe_pool);
	}

	// Delete keyframe at frame_index
	void deleteKeyframeAtFrame(int timeline_id, int frame_index) {
		Timeline* timeline = getTimelineForEdit(timeline_id);
		if (!timeline) return;

		int kf_id = timeline->getKeyframeAtFrame(frame_index, keyframe_pool);
		if (kf_id >= 0) {
			timeline->deleteKeyframe(kf_id, keyframe_pool);
		}
	}

	// Insert n blank frames at position frame_index
	// All spans starting at or after frame_index are shifted forward
	void insertFramesAt(int timeline_id, int frame_index, int count) {
		if (count <= 0) return;

		Timeline* timeline = getTimelineForEdit(timeline_id);
		if (!timeline) return;

		// Update frame count
		setFrameCount(frame_count + count);

		// Shift all spans at or after frame_index
		timeline->shiftSpansAfter(frame_index, count);
	}

	// Delete n frames starting at frame_index [frame_index, frame_index + count)
	// Spans are truncated/removed, and spans after the deleted range are shifted left
	void deleteFramesRange(int timeline_id, int frame_index, int count) {
		if (count <= 0) return;

		Timeline* timeline = getTimelineForEdit(timeline_id);
		if (!timeline) return;

		// Update frame count
		setFrameCount(std::max(1, frame_count - count));

		// Truncate/remove spans in deletion range
		timeline->removeFramesRange(frame_index, count, keyframe_pool);
	}

	// Set tween for a span
	// The span is found by searching for a span starting at frame_index
	void setSpanTween(int timeline_id, int frame_index, std::unique_ptr<FrameTween> tween) {
		Timeline* timeline = getTimelineForEdit(timeline_id);
		if (!timeline) return;

		auto spans = timeline->getMutableSpans();
		for (auto& span : spans) {
			if (span.start == frame_index) {
				span.setTween(std::move(tween));
				return;
			}
		}
	}

	// Get tween for a frame (finds span covering frame)
	const FrameTween* getFrameTween(int timeline_id, int frame_index) const {
		const Timeline* timeline = getTimeline(timeline_id);
		if (!timeline) return nullptr;

		const auto* span = timeline->getFrameSpanAt(frame_index);
		return span ? span->tween.get() : nullptr;
	}

	// Get interpolation value at frame for a span
	// Returns 0.0-1.0 based on tween evaluation
	float getFrameTweenValue(int timeline_id, int frame_index) const {
		const Timeline* timeline = getTimeline(timeline_id);
		if (!timeline) return 0.0f;

		const auto* span = timeline->getFrameSpanAt(frame_index);
		return span ? span->getTweenValue(frame_index) : 0.0f;
	}

private:
	int id = -1;
	int frame_count = 120;
	int fps = 24;  // Frames per second for playback speed
	std::vector<LayerData> layers;
	std::unordered_map<int, std::unique_ptr<Timeline>> timelines;  // timeline_id -> Timeline
	KeyframePool keyframe_pool;
	int next_layer_id = 0;
	int next_timeline_id = 0;
};

// ============================================================================
// SECTION 7: TIMELINE STATE - Current playback state
// ============================================================================

struct TimelineState {
	int current_timeline_id = -1;  // Currently selected timeline
	int current_frame = 0;          // Current frame for display/editing
	bool is_playing = false;
	::std::chrono::milliseconds last_frame_time{};
	::std::chrono::milliseconds frame_duration = ::std::chrono::milliseconds(1000) / 60;  // Fixed frame duration for all timelines

	void reset() {
		current_timeline_id = -1;
		current_frame = 0;
		is_playing = false;
		last_frame_time = {};
	}
};

// ============================================================================
// SECTION 8: TIMELINE INSTANCE - Runtime playback state
// ============================================================================

class TimelineInstance {
public:
	enum class PlayMode {
		PlayOnce,  // Play once then stop
		PlayLoop   // Loop indefinitely
	};

	TimelineInstance(std::shared_ptr<TimelineAsset> asset)
		: asset(asset), play_mode(PlayMode::PlayLoop), current_frame(0), is_playing(false) {}

	// Asset reference
	std::shared_ptr<TimelineAsset> getAsset() const { return asset; }

	// Playback state
	int getCurrentFrame() const { return current_frame; }
	void setCurrentFrame(int frame) {
		if (asset) {
			current_frame = std::max(0, std::min(frame, asset->getFrameCount() - 1));
		}
	}

	bool isPlaying() const { return is_playing; }
	void setPlaying(bool playing) { is_playing = playing; }

	PlayMode getPlayMode() const { return play_mode; }
	void setPlayMode(PlayMode mode) { play_mode = mode; }

	// Advance frame (called per update)
	void advanceFrame() {
		if (!is_playing || !asset) return;

		current_frame++;
		if (current_frame >= asset->getFrameCount()) {
			if (play_mode == PlayMode::PlayLoop) {
				current_frame = 0;
			} else {
				current_frame = asset->getFrameCount() - 1;
				is_playing = false;
			}
		}
	}

	// Reset playback
	void reset() {
		current_frame = 0;
		is_playing = false;
	}

private:
	std::shared_ptr<TimelineAsset> asset;
	PlayMode play_mode;
	int current_frame;
	bool is_playing;
};
