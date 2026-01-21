# Timeline System Quick Start Guide

Get started with the Adobe Animate CC-style timeline system in 5 minutes.

## TL;DR - The Key Concepts

1. **Discrete frames**: All timing uses integer frame indices (0, 1, 2, ...)
2. **FrameSpans**: Contiguous intervals of frames covered by one keyframe
3. **Keyframes define content**: Keyframes are placed at FrameSpan start positions
4. **Tweens interpolate**: Optional tweens provide smooth transitions between keyframes
5. **Separate asset and runtime**: `TimelineAsset` = data, `TimelineInstance` = playback

## 5-Minute Tutorial

### Step 1: Create an Asset

```cpp
#include "timeline_data.h"

// Create an animation asset: 120 frames @ 24 FPS
auto asset = std::make_shared<TimelineAsset>();
asset->setFrameCount(120);
asset->setFPS(24);
```

### Step 2: Create a Timeline (Layer)

```cpp
// Create timeline
int timeline_id = asset->createTimeline(120);

// Create editor layer metadata
int layer_id = asset->createLayer("Character");
```

### Step 3: Add Keyframes

```cpp
// Add keyframes at frames 0, 30, 60
int kf1 = asset->insertKeyframeAtFrame(timeline_id, 0);
int kf2 = asset->insertKeyframeAtFrame(timeline_id, 30);
int kf3 = asset->insertKeyframeAtFrame(timeline_id, 60);

// Each keyframe can contain display objects
auto& pool = asset->getMutableKeyframePool();
pool.addChildToKeyframe(kf1, 100);  // Object ID 100
pool.addChildToKeyframe(kf2, 101);  // Object ID 101
pool.addChildToKeyframe(kf3, 102);  // Object ID 102
```

### Step 4: Add Tweens (Optional)

```cpp
// Linear tween from frame 0-30
asset->setSpanTween(timeline_id, 0, 
    std::make_unique<LinearTween>());

// Ease-in tween from frame 30-60
asset->setSpanTween(timeline_id, 30, 
    std::make_unique<EaseInTween>());
```

### Step 5: Play It Back

```cpp
// Create playback instance
auto instance = std::make_unique<TimelineInstance>(asset);
instance->setPlayMode(TimelineInstance::PlayMode::PlayLoop);
instance->setPlaying(true);

// In your game/render loop:
void update() {
    if (instance->isPlaying()) {
        int current_frame = instance->getCurrentFrame();
        
        // Get active keyframe
        int kf_id = asset->getKeyframeAtFrame(timeline_id, current_frame);
        if (kf_id >= 0) {
            auto kf = asset->getKeyframe(kf_id);
            
            // Get tween interpolation [0.0, 1.0]
            float t = asset->getFrameTweenValue(timeline_id, current_frame);
            
            // Render keyframe's objects with interpolation
            for (int obj_id : kf->child_display_ids) {
                renderObject(obj_id, t);
            }
        }
        
        // Advance to next frame
        instance->advanceFrame();
    }
}
```

Done! ??

## Common Tasks

### Split a Keyframe (Insert Keyframe in Middle)

When you insert a keyframe in the middle of an existing FrameSpan, it automatically splits:

```cpp
// Original: [KF1: 0-30] [KF2: 30-60] [KF3: 60-90]
// Insert keyframe at frame 15
int new_kf = asset->insertKeyframeAtFrame(timeline_id, 15);
// Result: [KF1: 0-15] [New: 15-30] [KF2: 30-60] [KF3: 60-90]
```

### Insert Frames

Push existing content forward:

```cpp
// Insert 10 blank frames at position 50
asset->insertFramesAt(timeline_id, 50, 10);
// Everything at frame 50+ moves forward 10 frames
```

### Delete Frames

Remove content:

```cpp
// Delete 5 frames at position 20-25
asset->deleteFramesRange(timeline_id, 20, 5);
// Content at frame 25+ shifts left 5 frames
```

### Delete a Keyframe

Removes the entire FrameSpan:

```cpp
// Delete keyframe at frame 30
asset->deleteKeyframeAtFrame(timeline_id, 30);
// The entire span [30, X) is removed
```

### Query Animation Structure

```cpp
// Get keyframe covering frame 25
int kf_id = asset->getKeyframeAtFrame(timeline_id, 25);

// Get the FrameSpan covering frame 25
const auto* span = asset->getFrameSpanAt(timeline_id, 25);
if (span) {
    printf("Frame 25 is in span [%d, %d)\n", span->start, span->end);
}

// Get all keyframes
std::vector<int> all_kfs = asset->getTimelineForEdit(timeline_id)->getAllKeyframes(
    asset->getMutableKeyframePool()
);
```

## Understanding FrameSpans

A FrameSpan represents a contiguous interval of frames:

```
Timeline: [====KF1====][====KF2====][====KF3====]
Frames:   [0-----30---)[30-----60--)[60-----90--)
Keyframe:         KF1          KF2         KF3
```

**Key points:**
- Span `[30, 60)` = frames 30, 31, ..., 59 (60 is NOT included)
- Keyframe is only at span start (frame 30)
- Frames 31-59 derive from frame 30's keyframe via tween (if tween exists)
- If no tween: frames 31-59 hold the same value as frame 30 (stepped)

## Understanding Tweens

Tweens provide smooth transitions:

```
Without tween (stepped):
Frame:   0    10   20   30   40   50   60
Value:   [A---------A][B---------B][C------
         (held at A)  (held at B)

With tween (interpolated):
Frame:   0    10   20   30   40   50   60
Value:   [A...B/2.B.B][B...C/2.C.C][C------
         (interpolates)  (interpolates)
```

### Tween Types

- **LinearTween**: Smooth linear interpolation
- **EaseInTween**: Slow start ¡ú fast end (t?)
- **EaseOutTween**: Fast start ¡ú slow end (1-(1-t)?)

Create custom tweens by subclassing `FrameTween`:

```cpp
class MyCustomTween : public FrameTween {
public:
    float evaluate(int local_frame, int span_length) const override {
        float t = static_cast<float>(local_frame) / (span_length - 1);
        // Your custom curve here
        return t * t;  // quadratic
    }
    
    std::unique_ptr<FrameTween> clone() const override {
        return std::make_unique<MyCustomTween>();
    }
};
```

## Asset vs Instance

**TimelineAsset**: The animation data
- Created once, reused multiple times
- Contains keyframes, spans, tweens
- Immutable during playback (use const references)
- Serializable

**TimelineInstance**: The playback state
- One per active animation
- Tracks current frame, play mode
- Can have multiple instances of same asset playing independently
- Not serializable (state only)

```cpp
// One asset
auto asset = std::make_shared<TimelineAsset>(...);

// Multiple instances, playing independently
auto anim1 = std::make_unique<TimelineInstance>(asset);
auto anim2 = std::make_unique<TimelineInstance>(asset);

anim1->setCurrentFrame(0);
anim2->setCurrentFrame(30);
// Both play from different positions
```

## Debugging Tips

### Print Timeline Structure

```cpp
auto timeline = asset->getTimelineForEdit(timeline_id);
if (timeline) {
    for (const auto& span : timeline->getSpans()) {
        printf("Span [%d, %d): KF%d\n", 
            span.start, span.end, span.keyframe_id);
    }
}
```

### Check Frame Coverage

```cpp
for (int frame = 0; frame < asset->getFrameCount(); ++frame) {
    int kf_id = asset->getKeyframeAtFrame(timeline_id, frame);
    printf("Frame %d: KF%d\n", frame, kf_id);
}
```

### Verify Tween Values

```cpp
const auto* span = asset->getFrameSpanAt(timeline_id, 15);
if (span && span->tween) {
    float t = span->getTweenValue(15);
    printf("Tween value at frame 15: %.2f\n", t);
}
```

## Common Mistakes

? **Modifying asset during playback**
```cpp
// Don't do this:
instance->advanceFrame();
asset->insertKeyframeAtFrame(timeline_id, 50);  // Bad!
```
Use separate thread or pause before editing.

? **Do this:**
```cpp
instance->setPlaying(false);  // Pause
asset->insertKeyframeAtFrame(timeline_id, 50);  // Safe
instance->setPlaying(true);   // Resume
```

---

? **Assuming FrameSpan includes `end` frame**
```cpp
// Wrong: span [30, 60) covers frames 30-60
// Right: span [30, 60) covers frames 30-59 (60 is NOT included)
```

---

? **Forgetting to clone tweens for independence**
```cpp
auto tween1 = std::make_unique<LinearTween>();
auto tween2 = tween1.get();  // Sharing pointer! Don't do this.
asset->setSpanTween(timeline_id, 0, std::move(tween1));
// Now tween1 is moved, tween2 is dangling
```

? **Clone instead:**
```cpp
auto tween_original = std::make_unique<LinearTween>();
auto tween_copy = tween_original->clone();
asset->setSpanTween(timeline_id, 0, std::move(tween_copy));
```

---

? **Deleting a keyframe expecting only that keyframe to disappear**
```cpp
// Deleting a keyframe deletes the entire FrameSpan!
asset->deleteKeyframeAtFrame(timeline_id, 30);
// The span [30, 60) is GONE, not just frame 30
```

? **If you want to remove just that frame:**
```cpp
// Delete 1 frame at position 30
asset->deleteFramesRange(timeline_id, 30, 1);
// Only frame 30 is gone; [31, 60) shifts to [30, 59)
```

---

## Next Steps

1. Read [ANIMATE_CC_SEMANTICS.md](./ANIMATE_CC_SEMANTICS.md) for deep understanding
2. Check [TIMELINE_API_REFERENCE.md](./TIMELINE_API_REFERENCE.md) for complete API
3. Integrate with your editor UI (see `timeline_layer.h` and `timeline_ui.h`)
4. Add serialization (save/load)
5. Implement undo/redo with operation history

---

Happy animating! ??
