# Timeline System API Reference

Complete reference for the Adobe Animate CC-style timeline system.

## TimelineAsset

The main container for animation data. Represents an immutable animation asset ready for serialization or playback.

### Properties

```cpp
int getId() const;              // Unique asset identifier
int getFrameCount() const;      // Total frames in timeline
void setFrameCount(int count);  // Update total frames

int getFPS() const;             // Frames per second (playback speed)
void setFPS(int new_fps);       // Update playback speed
```

### Layer Management

```cpp
int getLayerCount() const;                                  // Number of layers
const LayerData* getLayerData(int index) const;            // Get layer at index
const LayerData* getLayerDataById(int layer_id) const;     // Find layer by ID

int createLayer(const std::string& name);                  // Create new layer
void updateLayerData(int layer_id, const LayerData& data); // Update layer
void deleteLayer(int layer_id);                             // Remove layer
```

### Timeline (Layer Timeline) Management

```cpp
int createTimeline(int frame_count = 120);         // Create new timeline for layer
Timeline* getTimelineForEdit(int timeline_id);      // Get timeline for editing
void deleteTimeline(int timeline_id);               // Remove timeline
```

### Frame Evaluation (Playback)

Read-only operations for evaluating animation at a given frame.

```cpp
// Get keyframe active at frame
int getKeyframeAtFrame(int timeline_id, int frame_index) const;

// Get keyframe at or before frame (closest previous keyframe)
int getKeyframeAtOrBeforeFrame(int timeline_id, int frame_index) const;

// Get the FrameSpan covering this frame
const Timeline::FrameSpan* getFrameSpanAt(int timeline_id, int frame_index) const;

// Get keyframe data
const TimelineKeyframe* getKeyframe(int keyframe_id) const;

// Get children in keyframe
std::vector<int> getKeyframeChildren(int keyframe_id) const;

// Get tween at frame (for interpolation)
const FrameTween* getFrameTween(int timeline_id, int frame_index) const;

// Evaluate tween value [0.0, 1.0]
float getFrameTweenValue(int timeline_id, int frame_index) const;
```

### Frame Editing

Mutable operations for editing animation structure.

```cpp
// Insert keyframe at frame_index
// Returns keyframe ID, or -1 on error
// If frame is within a span, splits the span automatically
int insertKeyframeAtFrame(int timeline_id, int frame_index);

// Delete keyframe at frame_index
// Removes entire FrameSpan at this position
void deleteKeyframeAtFrame(int timeline_id, int frame_index);

// Insert n blank frames at position frame_index
// Shifts all subsequent spans forward
void insertFramesAt(int timeline_id, int frame_index, int count);

// Delete n frames in range [frame_index, frame_index + count)
// Removes spans and shifts remaining content left
void deleteFramesRange(int timeline_id, int frame_index, int count);

// Set interpolation tween for span starting at frame_index
void setSpanTween(int timeline_id, int frame_index, 
                  std::unique_ptr<FrameTween> tween);
```

### Internal/Admin Access

```cpp
std::vector<LayerData>& getMutableLayers();                    // Direct layer access
std::unordered_map<int, std::unique_ptr<Timeline>>& getMutableTimelines();  // Timelines
KeyframePool& getMutableKeyframePool();                         // Keyframe pool
```

---

## TimelineKeyframe

Represents a single keyframe in the timeline.

### Properties

```cpp
int id;                           // Unique keyframe identifier
int frame_index;                  // Frame position (0-based)
int duration;                     // Duration in frames (for reference)
std::vector<int> child_display_ids;  // Objects contained in keyframe
```

### Construction

```cpp
TimelineKeyframe();                              // Default
TimelineKeyframe(int id, int frame_idx);        // With ID and frame
```

---

## FrameSpan

Represents a contiguous interval of frames covered by one keyframe.

### Properties

```cpp
int start;                              // Inclusive start frame
int end;                                // Exclusive end frame (start < end)
int keyframe_id;                        // Keyframe at start position
std::unique_ptr<FrameTween> tween;      // Optional interpolation
```

### Methods

```cpp
void setTween(std::unique_ptr<FrameTween> new_tween);  // Set or replace tween
float getTweenValue(int frame_index) const;             // Evaluate tween at frame
```

### Invariants

- `start < end` (must be valid interval)
- `keyframe_id >= 0` (always has a keyframe at start)
- No overlapping with other spans in same timeline
- Maintained in sorted order by `start`

---

## FrameTween Interface

Base class for frame interpolation within a FrameSpan.

### Virtual Methods

```cpp
virtual float evaluate(int local_frame, int span_length) const = 0;
// Evaluate tween position
// local_frame: 0 to span_length-1
// Returns: [0.0, 1.0] interpolation factor

virtual std::unique_ptr<FrameTween> clone() const = 0;
// Deep copy for serialization
```

### Built-in Implementations

#### LinearTween
Linear interpolation from 0.0 to 1.0.

```cpp
class LinearTween : public FrameTween {
public:
    float evaluate(int local_frame, int span_length) const override;
    std::unique_ptr<FrameTween> clone() const override;
};
```

**Behavior:**
- `evaluate(0, n)` ¡ú 0.0
- `evaluate(n-1, n)` ¡ú 1.0
- Linearly interpolated in between

#### EaseInTween
Cubic ease-in (slow start, fast end).

```cpp
class EaseInTween : public FrameTween {
public:
    float evaluate(int local_frame, int span_length) const override;
    std::unique_ptr<FrameTween> clone() const override;
};
```

**Behavior:**
- `t?` where t = frame_progress [0, 1]
- Starts slow, accelerates

#### EaseOutTween
Cubic ease-out (fast start, slow end).

```cpp
class EaseOutTween : public FrameTween {
public:
    float evaluate(int local_frame, int span_length) const override;
    std::unique_ptr<FrameTween> clone() const override;
};
```

**Behavior:**
- `1 - (1-t)?` where t = frame_progress [0, 1]
- Starts fast, decelerates

---

## Layer

Editor metadata for a timeline layer.

### Properties

```cpp
int id;                     // Unique layer ID
int timeline_id;            // Associated timeline
std::string name;           // Display name
int layer_order;            // Composite/stacking order
bool visible;               // Display in editor (doesn't affect evaluation)
bool locked;                // Prevent editing (doesn't affect playback)
bool solo;                  // Solo mode for editor
int selected_frame;         // Current editor selection
```

### Methods

```cpp
int getId() const;
int getTimelineId() const;

const std::string& getName() const;
void setName(const std::string& new_name);

bool isVisible() const;
void setVisible(bool v);

bool isLocked() const;
void setLocked(bool l);

bool isSolo() const;
void setSolo(bool s);

int getOrder() const;
void setOrder(int order);

int getSelectedFrame() const;
void setSelectedFrame(int frame);

bool hasFrameRangeSelection() const;
int getSelectionRangeStart() const;
int getSelectionRangeEnd() const;
void setFrameRangeSelection(int start, int end);
```

---

## Timeline

Manages FrameSpans and keyframes for a single layer's timeline.

### Properties

```cpp
int id;              // Timeline ID
int frame_count;     // Total frames
std::vector<FrameSpan> spans;  // Sorted by start frame
```

### Creation Methods

```cpp
int createKeyframe(int frame_index, KeyframePool& pool);
// Create/insert keyframe at frame
// Automatically splits spans if necessary
// Returns keyframe ID, or -1 if invalid frame

void deleteKeyframe(int keyframe_id, KeyframePool& pool);
// Delete keyframe and its span
```

### Query Methods

```cpp
int getKeyframeAtFrame(int frame_index, const KeyframePool& pool) const;
// Get keyframe exactly at frame (span start)
// Returns -1 if no keyframe at this exact frame

int getKeyframeAtOrBeforeFrame(int frame_index, const KeyframePool& pool) const;
// Get keyframe at or before frame (closest previous)

int getDisplayKeyframeAtFrame(int frame_index, const KeyframePool& pool) const;
// Get keyframe covering this frame (span evaluation)

const FrameSpan* getFrameSpanAt(int frame_index) const;
FrameSpan* getFrameSpanAt(int frame_index);
// Get span covering frame

std::vector<int> getAllKeyframes(const KeyframePool& pool) const;
// Get all keyframe IDs in timeline

const std::vector<FrameSpan>& getSpans() const;
std::vector<FrameSpan>& getMutableSpans();
// Access spans directly
```

### Frame Editing Methods

```cpp
void shiftSpansAfter(int frame_index, int offset);
// Shift all spans with start >= frame_index by offset
// Used for insert/delete operations

void removeFramesRange(int f, int n, KeyframePool& pool);
// Truncate/remove spans overlapping [f, f+n)
// Shift remaining spans left
```

### Configuration

```cpp
int getId() const;
int getFrameCount() const;
void setFrameCount(int count);
```

---

## TimelineInstance

Runtime playback state for a timeline asset.

### Enums

```cpp
enum class PlayMode {
    PlayOnce,   // Play once then stop
    PlayLoop    // Loop indefinitely
};
```

### Methods

```cpp
// Asset reference
std::shared_ptr<TimelineAsset> getAsset() const;

// Playback position
int getCurrentFrame() const;
void setCurrentFrame(int frame);

// Play state
bool isPlaying() const;
void setPlaying(bool playing);

// Play mode
PlayMode getPlayMode() const;
void setPlayMode(PlayMode mode);

// Frame advancement (called per update)
void advanceFrame();
// Advances current frame
// Loops or stops based on play mode and frame count

// Reset playback
void reset();
// Set to frame 0, stop playback
```

---

## TimelineState

Global editor playback state.

### Properties

```cpp
int current_timeline_id;   // Currently editing/viewing timeline
int current_frame;         // Current frame
bool is_playing;           // Is animation playing
double last_frame_time;    // Accumulator for frame timing
float frame_duration_ms;   // Duration per frame in milliseconds
```

### Methods

```cpp
void reset();
// Reset to initial state
```

---

## KeyframePool

Centralized storage for keyframes across all timelines.

### Methods

```cpp
// Creation and access
int createKeyframe(int frame_index);
TimelineKeyframe* getKeyframe(int id);
const TimelineKeyframe* getKeyframe(int id) const;
void deleteKeyframe(int id);

// Timeline association
std::vector<int> getKeyframesInTimeline(int timeline_id) const;
void addKeyframeToTimeline(int timeline_id, int keyframe_id);
void removeKeyframeFromTimeline(int timeline_id, int keyframe_id);

// Child management
void addChildToKeyframe(int keyframe_id, int child_id);
void removeChildFromKeyframe(int keyframe_id, int child_id);

// Cleanup
void clear();
```

---

## TimelinePool

Centralized storage for timelines across the asset.

### Methods

```cpp
// Timeline management
int createTimeline(int frame_count = 120);
Timeline* getTimeline(int id);
const Timeline* getTimeline(int id) const;
void deleteTimeline(int id, KeyframePool& keyframe_pool);

// Cleanup
void clear();
```

---

## Usage Examples

### Creating an Animation Asset

```cpp
// Create asset
auto asset = std::make_shared<TimelineAsset>(1, 120, 24);

// Create layer
int layer_id = asset->createLayer("Layer 1");

// Create timeline for layer
int timeline_id = asset->createTimeline(120);

// Add keyframes
int kf1 = asset->insertKeyframeAtFrame(timeline_id, 0);
int kf2 = asset->insertKeyframeAtFrame(timeline_id, 30);
int kf3 = asset->insertKeyframeAtFrame(timeline_id, 60);

// Add objects to keyframes
auto& pool = asset->getMutableKeyframePool();
pool.addChildToKeyframe(kf1, obj_id_1);
pool.addChildToKeyframe(kf2, obj_id_2);
pool.addChildToKeyframe(kf3, obj_id_3);

// Set tweens
asset->setSpanTween(timeline_id, 0, std::make_unique<LinearTween>());
asset->setSpanTween(timeline_id, 30, std::make_unique<EaseInTween>());
```

### Playback

```cpp
// Create instance
auto instance = std::make_unique<TimelineInstance>(asset);
instance->setPlayMode(TimelineInstance::PlayMode::PlayLoop);
instance->setPlaying(true);

// In render loop
void render() {
    int frame = instance->getCurrentFrame();
    
    // Get keyframe at this frame
    int kf_id = asset->getKeyframeAtFrame(timeline_id, frame);
    if (kf_id >= 0) {
        auto kf = asset->getKeyframe(kf_id);
        
        // Get tween interpolation
        float tween_t = asset->getFrameTweenValue(timeline_id, frame);
        
        // Render keyframe's children with tween value
        for (int child_id : kf->child_display_ids) {
            render(child_id, tween_t);  // Apply interpolation
        }
    }
    
    // Advance frame for next update
    instance->advanceFrame();
}
```

### Editing Operations

```cpp
// Insert 10 frames at position 50
asset->insertFramesAt(timeline_id, 50, 10);

// Delete 5 frames at position 20
asset->deleteFramesRange(timeline_id, 20, 5);

// Insert keyframe in middle of span (auto-splits)
int new_kf = asset->insertKeyframeAtFrame(timeline_id, 35);

// Remove specific keyframe
asset->deleteKeyframeAtFrame(timeline_id, 0);
```

---

## Design Patterns

### Read-Only Asset Access

For playback, use const references:
```cpp
const TimelineAsset* asset = ...;
int kf_id = asset->getKeyframeAtFrame(timeline_id, frame);  // Safe, read-only
```

### Editing Pattern

For editing, use shared_ptr and mutable access:
```cpp
auto asset = std::make_shared<TimelineAsset>(...);
int kf_id = asset->insertKeyframeAtFrame(timeline_id, frame);  // Modifies asset
```

### Cloning Tweens

Always clone tweens for independent copies:
```cpp
auto original_tween = std::make_unique<LinearTween>();
auto cloned_tween = original_tween->clone();  // Deep copy
```

---

This API is designed for correctness and clarity, following Adobe Animate CC semantics while providing modern C++ interfaces.
