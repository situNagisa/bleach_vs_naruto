# Timeline System - Developer Cheat Sheet

Quick reference for common operations.

## Creating & Playing Animations

### Minimal Example
```cpp
// Create asset
auto asset = std::make_shared<TimelineAsset>();
asset->setFrameCount(120);

// Create timeline
int timeline_id = asset->createTimeline(120);

// Add keyframes
int kf0 = asset->insertKeyframeAtFrame(timeline_id, 0);
int kf30 = asset->insertKeyframeAtFrame(timeline_id, 30);

// Play
auto instance = std::make_unique<TimelineInstance>(asset);
instance->setPlaying(true);

// Render loop
while (instance->isPlaying()) {
    int kf = asset->getKeyframeAtFrame(timeline_id, instance->getCurrentFrame());
    // ... render keyframe ...
    instance->advanceFrame();
}
```

## Frame Evaluation

```cpp
// Get keyframe active at frame
int kf_id = asset->getKeyframeAtFrame(timeline_id, frame);

// Get the span covering frame
const auto* span = asset->getFrameSpanAt(timeline_id, frame);

// Get tween interpolation [0.0, 1.0]
float t = asset->getFrameTweenValue(timeline_id, frame);

// Get keyframe data
auto kf = asset->getKeyframe(kf_id);
if (kf) {
    for (int child_id : kf->child_display_ids) {
        renderChild(child_id, t);  // t for interpolation
    }
}
```

## Editing Operations

### Insert Keyframe (auto-splits span)
```cpp
int new_kf = asset->insertKeyframeAtFrame(timeline_id, 25);
```

### Delete Keyframe (removes entire span)
```cpp
asset->deleteKeyframeAtFrame(timeline_id, 25);
```

### Delete Frame(s)
```cpp
// Delete 5 frames starting at 20
asset->deleteFramesRange(timeline_id, 20, 5);
```

### Insert Frame(s)
```cpp
// Insert 10 blank frames at position 50
asset->insertFramesAt(timeline_id, 50, 10);
```

### Add Tween
```cpp
// Linear tween from frame 0-30
asset->setSpanTween(timeline_id, 0, std::make_unique<LinearTween>());

// Ease-in from frame 30-60
asset->setSpanTween(timeline_id, 30, std::make_unique<EaseInTween>());
```

## Child Object Management

### Add objects to keyframe
```cpp
auto& pool = asset->getMutableKeyframePool();
pool.addChildToKeyframe(kf_id, object_id);
```

### Get objects from keyframe
```cpp
auto children = asset->getKeyframeChildren(kf_id);
for (int child_id : children) {
    // ... render ...
}
```

### Remove object from keyframe
```cpp
auto& pool = asset->getMutableKeyframePool();
pool.removeChildFromKeyframe(kf_id, object_id);
```

## Playback Control

### Create Instance
```cpp
auto instance = std::make_unique<TimelineInstance>(asset);
```

### Play
```cpp
instance->setPlayMode(TimelineInstance::PlayMode::PlayLoop);
instance->setPlaying(true);
```

### Pause
```cpp
instance->setPlaying(false);
```

### Stop & Reset
```cpp
instance->reset();  // Frame 0, not playing
```

### Seek
```cpp
instance->setCurrentFrame(50);
```

### Manual Frame Advance
```cpp
instance->advanceFrame();
```

## Querying Timeline Structure

### Get all keyframes
```cpp
auto timeline = asset->getTimelineForEdit(timeline_id);
auto all_keyframes = timeline->getAllKeyframes(
    asset->getMutableKeyframePool()
);
```

### Print timeline structure
```cpp
auto timeline = asset->getTimelineForEdit(timeline_id);
for (const auto& span : timeline->getSpans()) {
    printf("Span [%d, %d): KF%d\n", 
           span.start, span.end, span.keyframe_id);
}
```

### Find closest previous keyframe
```cpp
int prev_kf = asset->getKeyframeAtOrBeforeFrame(timeline_id, frame);
```

## FrameSpan Reference

```
[start, end) interval
start:      Inclusive start frame
end:        Exclusive end frame (NOT included)
keyframe_id: Keyframe at start
tween:      Optional interpolation
```

**Example:** FrameSpan `[10, 20)` covers frames 10-19 (20 is NOT included)

## Tween Types

| Type | Behavior |
|------|----------|
| `LinearTween` | Smooth linear: 0 ¡ú 1 |
| `EaseInTween` | Slow start, fast end: t? |
| `EaseOutTween` | Fast start, slow end: 1-(1-t)? |

## Common Patterns

### Split a Keyframe
```cpp
// Insert keyframe in middle of existing span
int new_kf = asset->insertKeyframeAtFrame(timeline_id, 15);
// Span [0, 30) becomes [0, 15) and [15, 30)
```

### Extend Animation
```cpp
asset->setFrameCount(240);  // Double length
```

### Copy Span with Tween
```cpp
// Create two tweens independently
auto tween1 = std::make_unique<LinearTween>();
auto tween2 = tween1->clone();  // Deep copy
asset->setSpanTween(timeline_id, 0, std::move(tween1));
asset->setSpanTween(timeline_id, 30, std::move(tween2));
```

### Batch Import Keyframes
```cpp
std::vector<int> frame_indices = {0, 15, 30, 45, 60};
for (int frame : frame_indices) {
    int kf = asset->insertKeyframeAtFrame(timeline_id, frame);
    // ... configure keyframe ...
}
```

## Debugging

### Print keyframe at frame
```cpp
int kf_id = asset->getKeyframeAtFrame(timeline_id, 25);
printf("Frame 25: KF%d\n", kf_id);
```

### Verify span coverage
```cpp
auto span = asset->getFrameSpanAt(timeline_id, 25);
if (span) {
    printf("Frame 25 covered by span [%d, %d)\n", 
           span->start, span->end);
}
```

### List all spans
```cpp
auto timeline = asset->getTimelineForEdit(timeline_id);
for (const auto& span : timeline->getSpans()) {
    auto kf = asset->getKeyframe(span.keyframe_id);
    printf("[%d, %d) -> KF%d (%d children)\n",
           span.start, span.end, span.keyframe_id,
           kf->child_display_ids.size());
}
```

## Do's and Don'ts

### ? DO
- Clone tweens: `tween->clone()`
- Pause before editing: `instance->setPlaying(false)`
- Use const refs for reading: `const TimelineAsset& asset`
- Create new instances for independent playback

### ? DON'T
- Modify asset during playback
- Share tween pointers
- Assume `end` frame is included
- Delete keyframe expecting partial deletion
- Forget to check null pointers

## Type Conversions

```cpp
// IDs
int timeline_id, keyframe_id, layer_id, object_id;

// Frames
int frame_index, current_frame;

// Flags
bool is_playing, visible, locked;

// Types
TimelineAsset*, TimelineInstance*, FrameSpan*, FrameTween*

// Containers
std::vector<int>                       // ID lists
std::unordered_map<int, unique_ptr<>>  // ID ¡ú Object maps
```

## Headers to Include

```cpp
#include "timeline_data.h"    // Core data structures

// Optional (for UI integration)
#include "timeline_layer.h"   // Layer UI
#include "timeline_mgmt.h"    // System management
```

## Memory Management

```cpp
// Asset (shared across instances)
auto asset = std::make_shared<TimelineAsset>();

// Instances (one per playback)
auto instance = std::make_unique<TimelineInstance>(asset);

// Tweens (owned by FrameSpans)
asset->setSpanTween(tid, 0, std::make_unique<LinearTween>());
// No manual cleanup needed
```

## Performance Tips

1. **Batch operations** - Do multiple edits, then render
2. **Cache keyframes** - Don't repeatedly query same frame
3. **Use const refs** - Reduce copying
4. **Limit frames** - Keep animation under 1000 frames
5. **Pool instances** - Reuse TimelineInstance objects

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Frame not covered by any keyframe | Check FrameSpan boundaries |
| Tween not interpolating | Verify tween is set with `setSpanTween()` |
| Animation stops early | Check PlayMode and frame count |
| Memory leak with tweens | Use `std::make_unique` and move semantics |
| Keyframe not where expected | Remember `[start, end)` is right-open |

---

**For complete documentation, see:**
- ANIMATE_CC_SEMANTICS.md - Architecture
- TIMELINE_API_REFERENCE.md - Full API
- ANIMATE_CC_QUICK_START.md - Tutorial
