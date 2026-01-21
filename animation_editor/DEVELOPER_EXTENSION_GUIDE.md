# Timeline System - Developer Extension Guide

This guide helps developers extend the timeline system with custom features.

## ?? Architecture Overview

```cpp
// Core dependency chain:
// 
// FrameTween (abstract)
//    ¡ü
// FrameSpan (contains tween)
//    ¡ü
// Timeline (contains spans)
//    ¡ü
// KeyframePool (stores keyframes)
// TimelineAsset (owns everything)
//    ¡ü
// TimelineInstance (references asset)
```

## ?? Creating Custom Tweens

### 1. Subclass FrameTween

```cpp
class MyCustomTween : public FrameTween {
public:
    float evaluate(int local_frame, int span_length) const override {
        // local_frame: 0 to span_length-1
        // Return: interpolation [0.0, 1.0]
        float t = static_cast<float>(local_frame) / (span_length - 1);
        // Your curve here
        return t * t;  // Example: quadratic
    }
    
    std::unique_ptr<FrameTween> clone() const override {
        return std::make_unique<MyCustomTween>();
    }
};
```

### 2. Use in Animation

```cpp
asset->setSpanTween(timeline_id, frame_index, 
    std::make_unique<MyCustomTween>());
```

### 3. Advanced: Tween with Parameters

```cpp
class PowerTween : public FrameTween {
private:
    float power;
    
public:
    PowerTween(float p = 2.0f) : power(p) {}
    
    float evaluate(int local_frame, int span_length) const override {
        float t = static_cast<float>(local_frame) / (span_length - 1);
        float result = 1.0f;
        for (int i = 0; i < (int)power; i++) {
            result *= t;
        }
        return result;
    }
    
    std::unique_ptr<FrameTween> clone() const override {
        return std::make_unique<PowerTween>(power);
    }
};
```

## ?? Adding Serialization

### 1. Save to JSON

```cpp
#include <nlohmann/json.hpp>

using json = nlohmann::json;

json serializeTimeline(const TimelineAsset* asset, int timeline_id) {
    json data;
    data["frame_count"] = asset->getFrameCount();
    data["fps"] = asset->getFPS();
    
    // Serialize layers
    json layers = json::array();
    for (int i = 0; i < asset->getLayerCount(); i++) {
        auto layer = asset->getLayerData(i);
        layers.push_back({
            {"id", layer->id},
            {"name", layer->name},
            {"visible", layer->visible},
            {"locked", layer->locked}
        });
    }
    data["layers"] = layers;
    
    // Serialize timeline spans
    auto timeline = asset->getTimelineForEdit(timeline_id);
    json spans = json::array();
    for (const auto& span : timeline->getSpans()) {
        auto kf = asset->getKeyframe(span.keyframe_id);
        spans.push_back({
            {"start", span.start},
            {"end", span.end},
            {"keyframe", span.keyframe_id},
            {"children", kf->child_display_ids}
        });
    }
    data["spans"] = spans;
    
    return data;
}
```

### 2. Load from JSON

```cpp
std::shared_ptr<TimelineAsset> deserializeTimeline(const json& data) {
    auto asset = std::make_shared<TimelineAsset>();
    asset->setFrameCount(data["frame_count"]);
    asset->setFPS(data["fps"]);
    
    // Create timeline
    int timeline_id = asset->createTimeline(data["frame_count"]);
    
    // Restore spans
    for (const auto& span_data : data["spans"]) {
        int kf_id = asset->insertKeyframeAtFrame(
            timeline_id, span_data["start"]);
        
        // Restore children
        auto& pool = asset->getMutableKeyframePool();
        for (int child_id : span_data["children"]) {
            pool.addChildToKeyframe(kf_id, child_id);
        }
    }
    
    return asset;
}
```

## ?? Implementing Undo/Redo

### 1. Command Pattern

```cpp
class TimelineCommand {
public:
    virtual ~TimelineCommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string describe() const = 0;
};

class InsertKeyframeCommand : public TimelineCommand {
private:
    std::shared_ptr<TimelineAsset> asset;
    int timeline_id;
    int frame_index;
    int keyframe_id;
    
public:
    InsertKeyframeCommand(std::shared_ptr<TimelineAsset> a, 
                         int tid, int frame)
        : asset(a), timeline_id(tid), frame_index(frame), 
          keyframe_id(-1) {}
    
    void execute() override {
        keyframe_id = asset->insertKeyframeAtFrame(
            timeline_id, frame_index);
    }
    
    void undo() override {
        asset->deleteKeyframeAtFrame(timeline_id, frame_index);
    }
    
    std::string describe() const override {
        return "Insert keyframe at frame " + 
               std::to_string(frame_index);
    }
};
```

### 2. Command History

```cpp
class TimelineHistory {
private:
    std::vector<std::unique_ptr<TimelineCommand>> history;
    size_t current_position = 0;
    
public:
    void execute(std::unique_ptr<TimelineCommand> cmd) {
        // Truncate redo stack
        history.erase(history.begin() + current_position, 
                      history.end());
        
        // Execute and record
        cmd->execute();
        history.push_back(std::move(cmd));
        current_position++;
    }
    
    void undo() {
        if (current_position > 0) {
            current_position--;
            history[current_position]->undo();
        }
    }
    
    void redo() {
        if (current_position < history.size()) {
            history[current_position]->execute();
            current_position++;
        }
    }
};
```

## ?? Adding Nested Timelines

### 1. Extend FrameSpan

```cpp
struct FrameSpan {
    // ... existing fields ...
    int nested_timeline_id = -1;  // Optional nested timeline
    
    bool hasNestedTimeline() const {
        return nested_timeline_id >= 0;
    }
};
```

### 2. Evaluation with Nesting

```cpp
void evaluateFrameRecursive(const TimelineAsset* asset, 
                            int timeline_id, 
                            int frame_index,
                            std::vector<int>& out_display_ids) {
    // Get keyframe at frame
    const auto* span = asset->getFrameSpanAt(timeline_id, frame_index);
    if (!span) return;
    
    auto kf = asset->getKeyframe(span->keyframe_id);
    if (!kf) return;
    
    // Add keyframe's children
    out_display_ids.insert(out_display_ids.end(),
                          kf->child_display_ids.begin(),
                          kf->child_display_ids.end());
    
    // Recursively evaluate nested timeline
    if (span->hasNestedTimeline()) {
        evaluateFrameRecursive(asset, 
                              span->nested_timeline_id,
                              frame_index,
                              out_display_ids);
    }
}
```

## ?? Adding Shape Tweening

### 1. Extend Keyframe

```cpp
struct TimelineKeyframe {
    // ... existing fields ...
    std::vector<Vector2> shape_points;  // For shape tweening
    std::string shape_type;  // "polygon", "circle", etc.
};
```

### 2. Shape Tween

```cpp
class ShapeTween : public FrameTween {
private:
    const TimelineKeyframe* start_kf;
    const TimelineKeyframe* end_kf;
    
public:
    ShapeTween(const TimelineKeyframe* s, const TimelineKeyframe* e)
        : start_kf(s), end_kf(e) {}
    
    float evaluate(int local_frame, int span_length) const override {
        float t = static_cast<float>(local_frame) / (span_length - 1);
        
        // Morph shapes
        if (start_kf && end_kf) {
            std::vector<Vector2> morphed;
            int point_count = std::min(start_kf->shape_points.size(),
                                      end_kf->shape_points.size());
            
            for (int i = 0; i < point_count; i++) {
                Vector2 p = start_kf->shape_points[i] * (1 - t) +
                           end_kf->shape_points[i] * t;
                morphed.push_back(p);
            }
            
            // Render morphed shape...
        }
        
        return t;
    }
    
    std::unique_ptr<FrameTween> clone() const override {
        return std::make_unique<ShapeTween>(start_kf, end_kf);
    }
};
```

## ?? Adding Frame Events

### 1. Extend Keyframe

```cpp
typedef std::function<void()> FrameCallback;

struct TimelineKeyframe {
    // ... existing fields ...
    std::vector<FrameCallback> on_frame_enter;
    std::vector<FrameCallback> on_frame_exit;
};
```

### 2. Fire Events

```cpp
class TimelineEventManager {
private:
    std::unordered_map<int, int> last_frame;  // timeline -> last_frame
    const TimelineAsset* asset;
    
public:
    TimelineEventManager(const TimelineAsset* a) : asset(a) {}
    
    void update(int timeline_id, int current_frame) {
        int prev_frame = last_frame[timeline_id];
        
        if (current_frame != prev_frame) {
            // Fire exit events for previous frame
            int prev_kf_id = asset->getKeyframeAtFrame(
                timeline_id, prev_frame);
            if (prev_kf_id >= 0) {
                auto prev_kf = asset->getKeyframe(prev_kf_id);
                for (auto& cb : prev_kf->on_frame_exit) {
                    cb();
                }
            }
            
            // Fire enter events for current frame
            int cur_kf_id = asset->getKeyframeAtFrame(
                timeline_id, current_frame);
            if (cur_kf_id >= 0) {
                auto cur_kf = asset->getKeyframe(cur_kf_id);
                for (auto& cb : cur_kf->on_frame_enter) {
                    cb();
                }
            }
            
            last_frame[timeline_id] = current_frame;
        }
    }
};
```

## ?? Thread Safety

### Add Mutex for Multi-threaded Access

```cpp
class ThreadSafeTimelineAsset {
private:
    std::shared_ptr<TimelineAsset> asset;
    mutable std::mutex asset_mutex;
    
public:
    int getKeyframeAtFrame(int timeline_id, int frame) const {
        std::lock_guard<std::mutex> lock(asset_mutex);
        return asset->getKeyframeAtFrame(timeline_id, frame);
    }
    
    int insertKeyframeAtFrame(int timeline_id, int frame) {
        std::lock_guard<std::mutex> lock(asset_mutex);
        return asset->insertKeyframeAtFrame(timeline_id, frame);
    }
    
    // ... other methods ...
};
```

## ?? Performance Optimization

### Caching Frame Lookups

```cpp
class CachedTimelineAsset {
private:
    std::shared_ptr<TimelineAsset> asset;
    mutable std::unordered_map<int, int> frame_cache;  // frame -> kf_id
    mutable int last_timeline = -1;
    
public:
    int getKeyframeAtFrame(int timeline_id, int frame) {
        if (timeline_id != last_timeline) {
            frame_cache.clear();
            last_timeline = timeline_id;
        }
        
        auto it = frame_cache.find(frame);
        if (it != frame_cache.end()) {
            return it->second;
        }
        
        int kf_id = asset->getKeyframeAtFrame(timeline_id, frame);
        frame_cache[frame] = kf_id;
        return kf_id;
    }
};
```

## ?? Testing Framework

```cpp
class TimelineTest {
protected:
    std::shared_ptr<TimelineAsset> asset;
    
    void SetUp() {
        asset = std::make_shared<TimelineAsset>();
        asset->setFrameCount(120);
    }
    
    void TestFrameSpanSemantics() {
        int timeline_id = asset->createTimeline(120);
        
        // Insert keyframes
        int kf0 = asset->insertKeyframeAtFrame(timeline_id, 0);
        int kf30 = asset->insertKeyframeAtFrame(timeline_id, 30);
        
        // Test span coverage
        EXPECT_EQ(asset->getKeyframeAtFrame(timeline_id, 0), kf0);
        EXPECT_EQ(asset->getKeyframeAtFrame(timeline_id, 15), kf0);
        EXPECT_EQ(asset->getKeyframeAtFrame(timeline_id, 30), kf30);
    }
};
```

## ?? Best Practices

1. **Always clone tweens**: `tween->clone()` for independence
2. **Check null pointers**: Asset methods return nullptr on error
3. **Respect invariants**: Maintain FrameSpan validity
4. **Use move semantics**: `std::move(tween)` for efficiency
5. **Separate concerns**: Keep asset immutable during playback
6. **Document extensions**: Comment custom tween behavior
7. **Test edge cases**: Frame 0, last frame, empty spans

## ?? Resources

- **Architecture**: See `ANIMATE_CC_SEMANTICS.md`
- **API**: See `TIMELINE_API_REFERENCE.md`
- **Examples**: See `TIMELINE_CHEAT_SHEET.md`

---

This guide should help you extend the timeline system while maintaining its correctness and performance. Start small with a custom tween, then progress to more complex features!
