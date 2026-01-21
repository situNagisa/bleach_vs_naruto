# Timeline System - Quick Start Guide

## TL;DR - The New Architecture

```
Timeline System (Coordinator)
©À©¤©¤ KeyframePool (Storage)
©¦   ©¸©¤©¤ TimelineKeyframe (ID, frame_index, [child_ids])
©À©¤©¤ TimelinePool (Storage)
©¦   ©¸©¤©¤ Timeline (frame_count, keyframe management)
©À©¤©¤ TimelineUI (Rendering)
©¦   ©¸©¤©¤ RenderContext (position, size, scroll)
©¸©¤©¤ TimelineState (Playback state)
    ©¸©¤©¤ current_frame, is_playing, etc.
```

## 5-Minute Setup

### Step 1: Include Headers
```cpp
#include "timeline_data.h"
#include "timeline_layer.h"
#include "timeline_mgmt.h"
```

### Step 2: Add to Stage
```cpp
class Stage {
    TimelineSystem timeline_system;  // Add this
    // ... rest of Stage
};
```

### Step 3: Create Clip with Timeline
```cpp
Clip* clip = stage.createClip();
clip->player->load("animation.gif");
// Timeline automatically created!
```

### Step 4: Render Timeline
```cpp
// In main loop, during PHASE 4:
ImDrawList* draw_list = ImGui::GetWindowDrawList();
ImVec2 pos = ImGui::GetCursorScreenPos();
ImVec2 size = ImGui::GetContentRegionAvail();

stage.timeline_system.render(draw_list, pos, size);
```

### Step 5: Control Playback
```cpp
// Play/Pause
if (ImGui::Button("Play")) {
    stage.timeline_system.setPlaying(true);
}

// Seek to frame
stage.timeline_system.setCurrentFrame(50);

// Get current state
int frame = stage.timeline_system.getCurrentFrame();
bool playing = stage.timeline_system.isPlaying();
```

## Common Operations

### Create a Keyframe
```cpp
TimelineLayer* layer = stage.timeline_system.getLayer(clip_id);
if (layer) {
    int timeline_id = layer->getTimelineId();
    int kf_id = stage.timeline_system.createKeyframe(timeline_id, frame_index);
}
```

### Add Child to Keyframe
```cpp
stage.timeline_system.addChildToKeyframe(keyframe_id, display_object_id);
```

### Get Children at Current Frame
```cpp
int timeline_id = layer->getTimelineId();
int display_kf = stage.timeline_system.getDisplayKeyframe(
    timeline_id, 
    stage.timeline_system.getCurrentFrame()
);

if (display_kf >= 0) {
    std::vector<int> children = stage.timeline_system.getKeyframeChildren(display_kf);
    // Render/process these children
}
```

### Delete Keyframe
```cpp
stage.timeline_system.deleteKeyframe(timeline_id, keyframe_id);
```

## Key Concepts

### 1. Frame Continuation
Frames without keyframes reuse the last keyframe's data:
```
Keyframes at:    0 ......... 30 ......... 60
Displayed as:   [Kf0][Kf0]...[Kf30][Kf30]...[Kf60]
```
This saves memory - don't need data on every frame!

### 2. Layer = Clip + Timeline
Each clip has a corresponding layer:
- Layer metadata: name, visibility, lock status
- Timeline: frame count, keyframes
- DisplayObject IDs stored in keyframes, not on frames

### 3. Unified Playback State
All layers share the same `current_frame` and `is_playing`:
```cpp
// All clips update to same frame
stage.timeline_system.setCurrentFrame(50);
stage.timeline_system.setPlaying(true);
```

### 4. Data in Pools, Not on Objects
```cpp
// OLD (bad):
Frame frame;
frame.data = something;  // Data on frame

// NEW (good):
TimelineKeyframe* kf = pool.getKeyframe(kf_id);
kf->child_display_ids.push_back(id);  // Data in pool
```

## Rendering Details

### Timeline Panel Layout
```
©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
©¦ [Layer Label] ©¦ 0    5    10   15   20   25   30   ©¦
©À©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
©¦ Clip_0 (ID:0) ©¦ ¨€ ¨€  ¨„  ©¦  ©¦  ©¦  ©¦  ©¦  ©¦  ©¦  ©¦  ©¦ ©¦ (Keyframes at 0, 10, 25)
©¦ Clip_1 (ID:1) ©¦  ©¦  ©¦  ¨€ ¨€  ©¦  ©¦  ©¦  ©¦  ©¦  ©¦ ¨„ ©¦ ©¦
©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
  120px          Frame grid (15px per frame)
```

### RenderContext Settings
```cpp
TimelineUI::RenderContext ctx;
ctx.draw_list = draw_list;              // ImGui DrawList
ctx.panel_pos = position;               // Top-left corner
ctx.panel_size = size;                  // Width x Height
ctx.visible_frame_count = 30;           // Frames visible at once
ctx.start_frame_index = 0;              // First visible frame (for scrolling)
ctx.scroll_x = 0.0f;                    // Horizontal scroll amount
ctx.scroll_y = 0.0f;                    // Vertical scroll amount
```

## Accessing Components

```cpp
// Get Timeline System
TimelineSystem& timeline_sys = stage.timeline_system;

// Get TimelineUI
TimelineUI* ui = timeline_sys.getTimelineUI();

// Get TimelineState
TimelineState& state = timeline_sys.getState();

// Get specific layer
TimelineLayer* layer = timeline_sys.getLayer(clip_id);

// Get specific timeline
Timeline* timeline = timeline_sys.getTimeline(timeline_id);

// Get specific keyframe
TimelineKeyframe* kf = timeline_sys.getKeyframe(keyframe_id);
```

## Performance Tips

1. **Layer Count**: Reasonable limit ~50 layers
   - Each layer renders a row (40px default height)
   - ~12 visible at once in typical 200px panel

2. **Keyframe Count**: No practical limit
   - Only visible keyframes processed
   - O(K) complexity where K = keyframes in timeline

3. **Frame Count**: Set once, rarely changes
   - Default: 120 frames per layer
   - Changeable: `timeline->setFrameCount(240);`

4. **Update Rate**: Fixed 100ms per frame
   - Synchronized playback across all layers
   - Adjustable: `state.frame_duration_ms = 50.0f;`

## Debugging

### Print Timeline Info
```cpp
int layer_count = stage.timeline_system.getLayerCount();
int current_frame = stage.timeline_system.getCurrentFrame();
bool playing = stage.timeline_system.isPlaying();

std::cout << "Layers: " << layer_count 
          << ", Frame: " << current_frame
          << ", Playing: " << (playing ? "yes" : "no") << "\n";
```

### Check Keyframe Data
```cpp
TimelineKeyframe* kf = stage.timeline_system.getKeyframe(kf_id);
if (kf) {
    std::cout << "Keyframe " << kf->id 
              << " at frame " << kf->frame_index
              << " has " << kf->child_display_ids.size() << " children\n";
}
```

### Trace Display Keyframe
```cpp
int timeline_id = layer->getTimelineId();
int display_kf = stage.timeline_system.getDisplayKeyframe(timeline_id, current_frame);
std::cout << "At frame " << current_frame 
          << ", displaying keyframe " << display_kf << "\n";
```

## Next Steps

1. **Render keyframe markers** - Add visual indicators at keyframe positions
2. **Handle keyframe editing** - Double-click to edit, drag to move
3. **Add layer controls** - Show/hide, lock, solo, delete layer
4. **Implement scrubbing** - Drag playhead or click timeline
5. **Add keyboard shortcuts** - Play (spacebar), Frame advance (arrow keys)

See `TIMELINE_ARCHITECTURE.md` for detailed information.
