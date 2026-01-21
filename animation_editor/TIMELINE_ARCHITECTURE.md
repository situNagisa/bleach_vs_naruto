# Timeline System Refactoring - Complete Documentation

## Overview

The animation editor's timeline system has been completely refactored to support a robust, data-driven architecture. The new system separates concerns into distinct components:

1. **Data Pools** - Centralized storage for keyframes and timelines
2. **Timeline Layers** - UI representation and metadata for clips
3. **Timeline UI** - Rendering and interaction logic
4. **Timeline System** - Coordination between all components
5. **Stage Integration** - Display tree connection

## Architecture

### 1. Timeline Data Pool (`timeline_data.h`)

#### Components:

**TimelineKeyframe**
```cpp
struct TimelineKeyframe {
    int id;                              // Unique keyframe identifier
    int frame_index;                     // Where in timeline this keyframe is placed
    std::vector<int> child_display_ids;  // DisplayObject IDs in this keyframe
};
```

**KeyframePool**
- Centralized storage for all keyframes
- Manages keyframe creation, retrieval, and deletion
- Maps timelines to their keyframes
- Handles keyframe-child relationships

```cpp
KeyframePool pool;
int kf_id = pool.createKeyframe(frame_index);
TimelineKeyframe* kf = pool.getKeyframe(kf_id);
pool.addChildToKeyframe(kf_id, display_object_id);
```

**Timeline**
- Represents a single layer's timeline
- Fixed frame count (e.g., 120 frames)
- Manages keyframe creation and querying
- **Key Feature**: Keyframes can span multiple frames (frame continuation)
  - Frame at index 5 may not have a keyframe
  - It inherits the keyframe from frame 3 (last keyframe before frame 5)
  - This saves memory and allows smooth transitions

```cpp
Timeline timeline(timeline_id, 120);  // 120 frames
int kf_id = timeline.createKeyframe(10, pool);

// Query what keyframe to display at frame 50
int display_kf = timeline.getDisplayKeyframeAtFrame(50, pool);
// Returns the keyframe at or before frame 50
```

**TimelinePool**
- Manages all Timeline instances
- Each layer gets its own Timeline object
- Coordinates with KeyframePool for cleanup

```cpp
TimelinePool pool;
int timeline_id = pool.createTimeline(120);
Timeline* timeline = pool.getTimeline(timeline_id);
```

**TimelineState**
- Tracks playback state and current frame
- Shared across all timelines
- Fixed frame duration for all layers (synchronized playback)

### 2. Timeline Layer (`timeline_layer.h`)

**TimelineLayer**
- Wraps a Clip with timeline metadata
- Stores UI state (visibility, lock status, solo mode)
- Links clip to its timeline

```cpp
TimelineLayer layer(clip_id, timeline_id, "Layer Name");
layer.is_visible = true;
layer.is_locked = false;
layer.height = 40.0f;  // Render height in pixels
```

**TimelineUI**
- Renders the timeline panel interface
- Handles layer visualization
- Manages frame scrubbing and layer selection
- **Rendering Pipeline**:
  1. Background fill
  2. Frame header with numbers
  3. Layer rows (alternating colors)
  4. Frame grid
  5. Playhead line
  6. Keyframe markers (extensible)

```cpp
TimelineUI ui;
ui.addLayer(layer);

// Render context
TimelineUI::RenderContext ctx;
ctx.draw_list = draw_list;
ctx.panel_pos = panel_position;
ctx.panel_size = panel_size;
ctx.visible_frame_count = 30;
ctx.start_frame_index = 0;

ui.render(ctx, state);
```

### 3. Timeline System (`timeline_mgmt.h`)

Central coordination class that ties everything together:

```cpp
TimelineSystem timeline_system;

// Create timeline for new clip
Timeline* timeline = timeline_system.createTimeline(120);

// Create keyframe
int kf_id = timeline_system.createKeyframe(timeline_id, frame_index);

// Add child to keyframe
timeline_system.addChildToKeyframe(kf_id, display_object_id);

// Get display keyframe (with continuation)
int display_kf = timeline_system.getDisplayKeyframe(timeline_id, current_frame);

// Playback control
timeline_system.setPlaying(true);
timeline_system.setCurrentFrame(50);

// Rendering
timeline_system.render(draw_list, panel_pos, panel_size);
```

### 4. Stage Integration

The Stage class now includes a TimelineSystem instance:

```cpp
class Stage : public DisplayObjectContainer {
public:
    TimelineSystem timeline_system;
    
    Clip* createClip() {
        // ... create clip ...
        
        // Create corresponding timeline
        Timeline* timeline = timeline_system.createTimeline(120);
        timeline_system.createLayer(clip->id, timeline->getId(), "Clip_" + id);
        return clip;
    }
    
    void deleteClip(int clip_id) {
        // ... delete clip ...
        
        // Clean up timeline
        TimelineLayer* layer = timeline_system.getLayer(clip_id);
        if (layer) {
            timeline_system.deleteTimeline(layer->getTimelineId());
            timeline_system.deleteLayer(clip_id);
        }
    }
};
```

## Usage Examples

### Example 1: Create a clip with timeline

```cpp
Stage stage;

// Load GIF via drag-drop (automatic)
Clip* clip = stage.createClip();
clip->player->load("animation.gif");

// The timeline is created automatically
assert(stage.timeline_system.getLayerCount() == 1);
```

### Example 2: Create keyframes and add children

```cpp
int timeline_id = layer->getTimelineId();

// Create keyframes at specific frames
int kf_0 = stage.timeline_system.createKeyframe(timeline_id, 0);
int kf_30 = stage.timeline_system.createKeyframe(timeline_id, 30);
int kf_60 = stage.timeline_system.createKeyframe(timeline_id, 60);

// Add child DisplayObjects to keyframes
stage.timeline_system.addChildToKeyframe(kf_0, child_id_1);
stage.timeline_system.addChildToKeyframe(kf_30, child_id_2);
stage.timeline_system.addChildToKeyframe(kf_60, child_id_3);

// Frame 15 (between 0 and 30) will display children from keyframe 0
// Frame 45 (between 30 and 60) will display children from keyframe 30
```

### Example 3: Frame continuation behavior

```cpp
Timeline* timeline = stage.timeline_system.getTimeline(timeline_id);

// Create keyframes only at key points
stage.timeline_system.createKeyframe(timeline_id, 0);   // Start
stage.timeline_system.createKeyframe(timeline_id, 60);  // Mid
stage.timeline_system.createKeyframe(timeline_id, 119); // End

// Frames 0-59 use keyframe at 0 (60 frames of continuation)
// Frames 60-119 use keyframe at 60
// This saves memory compared to storing data on every frame
```

## Data Storage Strategy

### ? What's stored in pools:
- Keyframe metadata (ID, frame index)
- Child DisplayObject references
- Timeline configuration (frame count)
- Playback state (current frame, playing flag)

### ? What's NOT stored on individual frames:
- DisplayObject data (stored in keyframes instead)
- Transform data (stored separately)
- Animation parameters (stored separately)

### ? Benefits:
- **Memory efficient**: Keyframes only created where needed
- **Frame continuation**: Frames between keyframes reuse parent keyframe data
- **Decoupled**: Data separate from frame indices
- **Flexible**: Easy to add/remove keyframes without frame data corruption

## Rendering Pipeline

1. **Timeline Panel Setup** (in main loop):
   ```cpp
   ImDrawList* draw_list = ImGui::GetWindowDrawList();
   stage.timeline_system.render(draw_list, panel_pos, panel_size);
   ```

2. **TimelineUI::render()** calls:
   - `renderFrameHeader()` - Shows frame numbers
   - `renderLayer()` for each layer - Shows layer info and timeline
   - `renderPlayhead()` - Shows current frame indicator

3. **Layer Rendering**:
   - Background (alternating colors)
   - Label (layer name + clip ID)
   - Grid (frame divisions)
   - Borders (separation)
   - Keyframe markers (extensible - currently placeholder)

## Interaction Handling

### Mouse Interactions:
- **Click on frame**: Sets current_frame in TimelineState
- **Click on layer**: Selects that layer
- **Click on keyframe marker**: (Future) Select/edit keyframe

### Keyboard Interactions:
- **Play/Pause button**: Toggle playback
- **Timeline scrubber**: Drag to seek (Future)

## Future Enhancement Points

1. **Keyframe Visualization**
   - Add visual markers at keyframe positions
   - Color-code keyframes with data
   - Show keyframe duration span

2. **Keyframe Editing**
   - Right-click menu for keyframe operations
   - Drag keyframes to new positions
   - Insert/delete keyframes easily

3. **Multi-layer Editing**
   - Select multiple layers
   - Shift+click for range selection
   - Bulk operations

4. **Advanced Playback**
   - Loop range selection
   - Reverse playback
   - Variable playback speed

5. **Timeline Scrolling**
   - Horizontal scroll for frame visibility
   - Vertical scroll for layer visibility
   - Zoom in/out on timeline

## File Structure

```
animation_editor/
©À©¤©¤ main.cpp                 # Main application, uses TimelineSystem
©À©¤©¤ timeline_data.h          # Pool structures (KeyframePool, TimelinePool)
©À©¤©¤ timeline_layer.h         # TimelineLayer and TimelineUI classes
©À©¤©¤ timeline_mgmt.h          # TimelineSystem coordination class
©¸©¤©¤ timeline_*.md            # Documentation files
```

## Migration Notes

If migrating existing code to use the new system:

1. **Old**: Data stored on Frame struct
   ```cpp
   // OLD - Don't use
   Frame f;
   f.offset_x = 10.0f;
   ```

2. **New**: Data stored in pools
   ```cpp
   // NEW - Use this
   TimelineKeyframe* kf = timeline_system.getKeyframe(kf_id);
   // Store data in keyframe or elsewhere, not on frame
   ```

3. **Old**: Every frame iterated
   ```cpp
   for (int i = 0; i < frame_count; ++i) {
       // Process frame i
   }
   ```

4. **New**: Only keyframes iterated
   ```cpp
   std::vector<int> kfs = timeline->getAllKeyframes(pool);
   for (int kf_id : kfs) {
       // Process keyframe
   }
   ```

## Performance Characteristics

- **Memory**: O(K) where K = number of keyframes (not O(F) where F = number of frames)
- **Rendering**: O(K + V) where V = visible frames
- **Lookup**: O(1) for getDisplayKeyframe() with sorted keyframes
- **Iteration**: O(K) for keyframe operations

## API Reference

See class documentation in header files:
- `timeline_data.h` - Pool APIs
- `timeline_layer.h` - UI APIs
- `timeline_mgmt.h` - System APIs
