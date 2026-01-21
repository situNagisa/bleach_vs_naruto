# Adobe Animate CC Timeline Semantics

This document describes the complete implementation of an Adobe Animate CC-style timeline system for frame-by-frame animation editing.

## Core Design Philosophy

**This is an editor-level timeline system, not a runtime animation player.**

- All timing is discrete, integer-based (frame indices, no floating-point time)
- Frame index is the fundamental unit: `int frameIndex`
- FPS affects playback speed only, not the data structure
- Data is stored in **pools and containers**, not per-frame
- The system is designed for **editing and serialization**, with strong separation between asset data and runtime state

## Architecture Overview

### Layer 1: Data Structures (timeline_data.h)

#### 1. TimelineKeyframe
Represents a single keyframe at a specific frame index.

```cpp
struct TimelineKeyframe {
    int id;                          // Unique identifier
    int frame_index;                 // Frame where this keyframe exists
    int duration;                    // Duration in frames
    std::vector<int> child_display_ids;  // Objects in this keyframe
};
```

**Properties:**
- `id`: Globally unique within the asset
- `frame_index`: Position in the timeline (0-based)
- `duration`: How many frames this keyframe is active
- `child_display_ids`: Vector of DisplayObject IDs contained in this keyframe

#### 2. FrameSpan
Represents a contiguous interval of frames `[start, end)` covered by a single keyframe.

```cpp
struct FrameSpan {
    int start;                       // Inclusive start frame
    int end;                         // Exclusive end frame
    int keyframe_id;                 // Keyframe at start position
    std::unique_ptr<FrameTween> tween;  // Optional interpolation
};
```

**Invariants:**
- `start < end` (must be valid interval)
- `start` is always a keyframe position (Animate CC semantics: keyframe = start of span)
- No gaps between spans in a Timeline
- Spans are maintained in sorted order by `start`

**Semantics:**
- Frame `f` is covered by span `s` if `f >= s.start && f < s.end`
- Only one span covers any given frame
- The keyframe is only "active" at `start`; other frames in the span derive values via tween

#### 3. Timeline
Manages FrameSpans for a single layer's timeline.

```cpp
class Timeline {
public:
    int createKeyframe(int frame_index, KeyframePool& pool);
    void deleteKeyframe(int keyframe_id, KeyframePool& pool);
    
    int getDisplayKeyframeAtFrame(int frame_index, const KeyframePool& pool) const;
    const FrameSpan* getFrameSpanAt(int frame_index) const;
    
    void insertFrames(int frame_index, int count);
    void deleteFramesRange(int frame_index, int count, KeyframePool& pool);
};
```

**Operations:**

##### Creating a Keyframe at Frame `f`

1. **If no span exists**: Create new FrameSpan `[f, f+1)` with new keyframe
2. **If `f` matches an existing span's `start`**: Replace the keyframe at that position
3. **If `f` is within span `[s, e)` where `s < f < e`**: 
   - Split into `[s, f)` (keeps old keyframe) and `[f, e)` (gets new keyframe)
   - Maintains FrameSpan invariant

##### Deleting a Keyframe at Frame `f`

- Remove any FrameSpan whose start is at `f`
- Keyframes are tied to FrameSpan starts; deleting the keyframe removes the entire span

##### Frame Evaluation at Frame `f`

1. Find span `s` where `f >= s.start && f < s.end`
2. If span exists:
   - Return keyframe ID at `s.keyframe_id`
   - If span has tween: evaluate `getTweenValue(f - s.start, s.end - s.start)`
3. If no span exists: Frame is empty/blank

#### 4. KeyframePool
Centralized storage for all keyframes across all timelines.

```cpp
class KeyframePool {
public:
    int createKeyframe(int frame_index);
    TimelineKeyframe* getKeyframe(int id);
    void deleteKeyframe(int id);
    
    void addChildToKeyframe(int keyframe_id, int child_id);
    void removeChildFromKeyframe(int keyframe_id, int child_id);
};
```

**Role:**
- Maintains keyframe data in a single pool (not scattered across frames)
- Provides O(1) access to keyframe by ID
- Manages child object relationships

#### 5. FrameTween Interface
Defines interpolation behavior within a FrameSpan.

```cpp
class FrameTween {
public:
    virtual float evaluate(int local_frame, int span_length) const = 0;
    virtual std::unique_ptr<FrameTween> clone() const = 0;
};
```

**Built-in Tweens:**
- `LinearTween`: Linear interpolation from 0.0 to 1.0
- `EaseInTween`: Cubic ease-in (slow start, fast end)
- `EaseOutTween`: Cubic ease-out (fast start, slow end)

**Semantics:**
- `local_frame` ranges from 0 to `span_length - 1`
- Output is normalized to `[0.0, 1.0]`
- Used by properties to interpolate between keyframe values
- If span has no tween, value is stepped (change only at keyframe start)

#### 6. TimelineAsset
Immutable data container for serialization and editing.

```cpp
class TimelineAsset {
public:
    // Frame evaluation
    int getKeyframeAtFrame(int timeline_id, int frame_index) const;
    const FrameSpan* getFrameSpanAt(int timeline_id, int frame_index) const;
    float getFrameTweenValue(int timeline_id, int frame_index) const;
    
    // Frame editing
    int insertKeyframeAtFrame(int timeline_id, int frame_index);
    void deleteKeyframeAtFrame(int timeline_id, int frame_index);
    void insertFramesAt(int timeline_id, int frame_index, int count);
    void deleteFramesRange(int timeline_id, int frame_index, int count);
    void setSpanTween(int timeline_id, int frame_index, std::unique_ptr<FrameTween> tween);
};
```

**Structure:**
- Contains layers, timelines, and keyframes
- Provides immutable interface for reading
- Provides editing interface for construction/modification
- Handles all Animate CC semantics internally

#### 7. Layer
Editor-level metadata for a timeline layer.

```cpp
class Layer {
public:
    bool isVisible() const;
    bool isLocked() const;
    bool isSolo() const;
    int getOrder() const;  // Composite order
};
```

**Properties:**
- `visible`: Editor display only (doesn't affect evaluation)
- `locked`: Prevents editing operations
- `solo`: Editor focus mode
- `order`: Layer stacking for composition

#### 8. TimelineInstance
Runtime playback state separate from asset data.

```cpp
class TimelineInstance {
public:
    enum class PlayMode { PlayOnce, PlayLoop };
    
    int getCurrentFrame() const;
    void setCurrentFrame(int frame);
    bool isPlaying() const;
    void setPlaying(bool playing);
    
    PlayMode getPlayMode() const;
    void setPlayMode(PlayMode mode);
    
    void advanceFrame();  // Called per update
    void reset();
};
```

**Purpose:**
- Completely separate from asset data
- Holds playback position and mode
- Can have multiple instances of same asset playing independently

#### 9. TimelineState
Global playback state (editor-level).

```cpp
struct TimelineState {
    int current_timeline_id;
    int current_frame;
    bool is_playing;
    float frame_duration_ms;
};
```

## Frame Evaluation Algorithm

Given a timeline and frame index `f`, to determine what's displayed:

```
FOR EACH layer IN timeline:
    1. Find FrameSpan s where: s.start <= f < s.end
    
    IF s exists:
        2. Get keyframe kf at s.keyframe_id
        3. IF s.tween exists:
               t = (f - s.start) / (s.end - s.start)
               interpolated_value = lerp(kf.value, next_kf.value, t)
           ELSE:
               interpolated_value = kf.value  (stepped/held)
        4. Render keyframe kf with interpolated values
    ELSE:
        5. Layer is blank/empty at frame f
```

## Frame Editing Operations

### Insert Keyframe at Frame `f`

**Case 1: Frame `f` is empty (no span covers it)**
- Create new FrameSpan `[f, f+1)`
- Create new keyframe
- Insert at correct position maintaining order

**Case 2: Frame `f` matches existing span start**
- Replace the keyframe at that position
- Keep span boundaries unchanged

**Case 3: Frame `f` is within existing span `[s, e)`**
- Split span into `[s, f)` and `[f, e)`
- Left span keeps original keyframe
- Right span gets new keyframe
- Both spans are inserted maintaining order

### Delete Keyframe at Frame `f`

- Find FrameSpan where `start == f`
- Remove entire span (including all frames `[f, end)`)
- Delete keyframe from pool

**Important:** Deleting a keyframe removes the entire FrameSpan, clearing `end - start` frames.

### Insert `n` Blank Frames at Position `f`

1. Update timeline frame count: `count += n`
2. Shift all spans with `start >= f`:
   - `start += n`
   - `end += n`
3. Result: Blank space created, existing content pushed forward

**Example:**
```
Before: [KF1: 0-3] [KF2: 3-6] [KF3: 6-9]
Insert 2 at frame 3:
After:  [KF1: 0-3] [KF2: 5-8] [KF3: 8-11]
        Frames 3-4 are now blank
```

### Delete `n` Frames in Range `[f, f+n)`

1. Update timeline frame count: `count -= n`
2. For each span, apply one of:
   - **Entirely before `[f, f+n)`**: Keep unchanged
   - **Entirely after `[f, f+n)`**: Shift left by `n`
   - **Overlapping**: 
     - Truncate (shorten span to fit in available space)
     - Or remove if start falls inside deletion range
3. Remove any invalid spans where `start >= end`

**Example:**
```
Before: [KF1: 0-3] [KF2: 3-6] [KF3: 6-9]
Delete 2 frames at position 2:
After:  [KF1: 0-2] [KF2: 4-7]
        Frames 2-3 are deleted; KF2 and KF3 shift left
```

## Usage Patterns

### Creating a Simple Animation

```cpp
// Create asset
auto asset = std::make_shared<TimelineAsset>(1, 120, 24);

// Create timeline (layer)
int timeline_id = asset->createTimeline(120);

// Create keyframes
int kf1_id = asset->insertKeyframeAtFrame(timeline_id, 0);
int kf2_id = asset->insertKeyframeAtFrame(timeline_id, 30);
int kf3_id = asset->insertKeyframeAtFrame(timeline_id, 60);

// Add objects to keyframes
auto& keyframe_pool = asset->getMutableKeyframePool();
keyframe_pool.addChildToKeyframe(kf1_id, display_obj_id_1);
keyframe_pool.addChildToKeyframe(kf2_id, display_obj_id_2);

// Set tween (linear interpolation from kf2 to kf3)
asset->setSpanTween(timeline_id, 30, std::make_unique<LinearTween>());
```

### Editing a Timeline

```cpp
// Insert 10 frames at position 50
asset->insertFramesAt(timeline_id, 50, 10);

// Delete 5 frames starting at position 20
asset->deleteFramesRange(timeline_id, 20, 5);

// Add a keyframe in the middle of an existing span
// This splits the span automatically
int new_kf = asset->insertKeyframeAtFrame(timeline_id, 45);
```

### Playing an Animation

```cpp
// Create runtime instance
auto instance = std::make_unique<TimelineInstance>(asset);
instance->setPlayMode(TimelineInstance::PlayMode::PlayLoop);
instance->setPlaying(true);

// In update loop
void update(double delta_time) {
    for (auto& instance : instances) {
        if (instance->isPlaying()) {
            instance->advanceFrame();
            
            // Get keyframe at current frame
            int current_frame = instance->getCurrentFrame();
            const auto* keyframe = asset->getKeyframe(
                asset->getKeyframeAtFrame(timeline_id, current_frame)
            );
            
            if (keyframe) {
                // Render keyframe's children
                for (int child_id : keyframe->child_display_ids) {
                    render(child_id);
                }
            }
        }
    }
}
```

## Invariants

1. **FrameSpan validity**: `start < end` always
2. **No keyframe gaps**: All keyframes are at FrameSpan starts
3. **No overlapping spans**: No frame is covered by two spans
4. **Frame coverage**: Editing operations maintain complete frame coverage when possible
5. **ID uniqueness**: All keyframe/timeline/layer IDs are globally unique
6. **Tween clone-able**: All tween implementations support deep cloning for serialization

## Differences from Flash/Animate CC

While following Flash/Animate CC timeline semantics, this implementation diverges in:

1. **No ActionScript**: No script execution
2. **No symbols/clips**: Each layer is a simple frame sequence
3. **No movie clips**: No nested timelines (future enhancement)
4. **No guides**: Simplified UI layer
5. **Simplified tweens**: Basic easing only (can be extended)

## Future Enhancements

1. **Bezier/custom tweens**: More sophisticated interpolation curves
2. **Shape tweening**: Morphing between different shapes
3. **Nested timelines**: Movie clips with their own timelines
4. **Frame events**: Callbacks at specific frames
5. **Layer hierarchy**: Nested layer groups
6. **Undo/redo**: Operation history
7. **Serialization**: Save/load to standard formats (SVG, JSON, etc.)

---

This system provides a solid, semantically-correct foundation for frame-by-frame animation editing, following the proven Animate CC model.
