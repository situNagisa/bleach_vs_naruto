# Timeline System - Visual Reference Guide

## System Architecture Diagram

```
©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
©¦                         Stage                                   ©¦
©¦  ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´  ©¦
©¦  ©¦           TimelineSystem (Coordinator)                  ©¦  ©¦
©¦  ©¦                                                           ©¦  ©¦
©¦  ©¦  ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´  ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´  ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´  ©¦  ©¦
©¦  ©¦  ©¦KeyframePool  ©¦  ©¦TimelinePool  ©¦  ©¦TimelineUI    ©¦  ©¦  ©¦
©¦  ©¦  ©¦©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¦  ©¦©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¦  ©¦©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¦  ©¦  ©¦
©¦  ©¦  ©¦Keyframes     ©¦  ©¦Timelines     ©¦  ©¦Render logic  ©¦  ©¦  ©¦
©¦  ©¦  ©¦Kf¡úChildren   ©¦  ©¦frame_count   ©¦  ©¦Layer display ©¦  ©¦  ©¦
©¦  ©¦  ©¦Child storage ©¦  ©¦Kf queries    ©¦  ©¦Grid & markers©¦  ©¦  ©¦
©¦  ©¦  ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼  ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼  ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼  ©¦  ©¦
©¦  ©¦                                                           ©¦  ©¦
©¦  ©¦         ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´            ©¦  ©¦
©¦  ©¦         ©¦    TimelineState                ©¦            ©¦  ©¦
©¦  ©¦         ©¦  - current_frame                ©¦            ©¦  ©¦
©¦  ©¦         ©¦  - is_playing                   ©¦            ©¦  ©¦
©¦  ©¦         ©¦  - frame_duration_ms            ©¦            ©¦  ©¦
©¦  ©¦         ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼            ©¦  ©¦
©¦  ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼  ©¦
©¦                                                                 ©¦
©¦  ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´  ©¦
©¦  ©¦              Children (Clips)                            ©¦  ©¦
©¦  ©¦  ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´  ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´  ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´             ©¦  ©¦
©¦  ©¦  ©¦ Clip 0   ©¦  ©¦ Clip 1   ©¦  ©¦ Clip 2   ©¦             ©¦  ©¦
©¦  ©¦  ©¦(Timeline0)©¦ ©¦(Timeline1)©¦ ©¦(Timeline2)©¦             ©¦  ©¦
©¦  ©¦  ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼  ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼  ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼             ©¦  ©¦
©¦  ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼  ©¦
©¦                                                                 ©¦
©¦  Associated DisplayObjects (in Keyframe pools)                 ©¦
©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
```

## Data Flow: Creating a Keyframe

```
User creates keyframe at frame 30
         ¡ý
TimelineSystem.createKeyframe(timeline_id, 30)
         ¡ý
Timeline.createKeyframe(30, keyframe_pool)
         ¡ý
KeyframePool.createKeyframe(30) ¡ú creates TimelineKeyframe(id=5, frame_index=30)
         ¡ý
KeyframePool.addKeyframeToTimeline(timeline_id, kf_id)
         ¡ý
Keyframe stored in pool, ready for data
         ¡ý
TimelineSystem.addChildToKeyframe(kf_id, child_id)
         ¡ý
KeyframePool adds child_id to keyframe's child_display_ids vector
         ¡ý
? Keyframe ready for rendering and playback
```

## Frame Continuation Flow

```
Timeline with 3 keyframes:

Frame Index:   0    10    20    30    40    50    60    70    80
             ©°©¤KF0©¤©¤©Ð©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©´KF30©¤©¤©Ð©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©Ð©¤KF60©¤©¤©Ð©¤©¤©¤©¤©¤©´
Keyframes:   ©¦  ?   ©¦  .  ©¦  .  ©¦  ?   ©¦  .  ©¦  .  ©¦  ?   ©¦  .  ©¦
             ©¸©¤©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©¼
                ¡ø                  ¡ø                  ¡ø
            Frame 0-29         Frame 30-59        Frame 60-79
            use KF0            use KF30           use KF60

Display Logic:
  Frame 15: getDisplayKeyframe(15) ¡ú searches backward ¡ú returns KF0
  Frame 45: getDisplayKeyframe(45) ¡ú searches backward ¡ú returns KF30
  Frame 75: getDisplayKeyframe(75) ¡ú searches backward ¡ú returns KF60

Result: Memory efficient! Each keyframe's data reused for multiple frames.
```

## Rendering Pipeline

```
Stage.timeline_system.render(draw_list, pos, size)
         ¡ý
    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
    ©¦  TimelineUI::render()      ©¦
    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
         ¡ý
    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
    ©¦ 1. Draw background                              ©¦
    ©¦    AddRectFilled(pos, end, dark_color)          ©¦
    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
         ¡ý
    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
    ©¦ 2. Render frame header                          ©¦
    ©¦    - Frame numbers every 5 frames               ©¦
    ©¦    - Grid lines                                 ©¦
    ©¦    - Column headers (0, 5, 10, 15, ...)         ©¦
    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
         ¡ý
    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
    ©¦ 3. Render each layer                            ©¦
    ©¦    For each visible layer:                      ©¦
    ©¦    - Background (alternating color)             ©¦
    ©¦    - Layer label (left side)                    ©¦
    ©¦    - Timeline grid (right side)                 ©¦
    ©¦    - Frame lines (major: every 5, minor: 1)    ©¦
    ©¦    - Borders                                    ©¦
    ©¦    - Keyframe markers (extensible)              ©¦
    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
         ¡ý
    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
    ©¦ 4. Render playhead                              ©¦
    ©¦    - Vertical line at current frame position    ©¦
    ©¦    - Red color for visibility                   ©¦
    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
         ¡ý
    ? Timeline rendered complete
```

## Memory Layout Example

### Before Refactoring (Per-Frame Storage)
```
Memory for 120 frames, 3 keyframes of data:

Frame[0]  ¡ú data: {...}
Frame[1]  ¡ú data: {...}  (duplicate)
Frame[2]  ¡ú data: {...}  (duplicate)
...
Frame[29] ¡ú data: {...}  (duplicate)
Frame[30] ¡ú data: {...}  (different)
Frame[31] ¡ú data: {...}  (duplicate)
...
Frame[119] ¡ú data: {...}  (duplicate)

Total: 120 ¡Á sizeof(FrameData) ¡Ö ~120KB for simple data
```

### After Refactoring (Pool-Based)
```
KeyframePool:
©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
©¦ Keyframe 0 (at frame 0)     ©¦
©¦ - id: 0                     ©¦
©¦ - frame_index: 0            ©¦
©¦ - children: [...]           ©¦
©À©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
©¦ Keyframe 1 (at frame 30)    ©¦
©¦ - id: 1                     ©¦
©¦ - frame_index: 30           ©¦
©¦ - children: [...]           ©¦
©À©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
©¦ Keyframe 2 (at frame 60)    ©¦
©¦ - id: 2                     ©¦
©¦ - frame_index: 60           ©¦
©¦ - children: [...]           ©¦
©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼

TimelineState:
- current_frame: 15 ¡ú queries KeyframePool ¡ú gets Keyframe 0
- current_frame: 45 ¡ú queries KeyframePool ¡ú gets Keyframe 1
- current_frame: 75 ¡ú queries KeyframePool ¡ú gets Keyframe 2

Total: 3 ¡Á sizeof(KeyframeData) ¡Ö ~3KB
Savings: ~97% memory reduction!
```

## Layer and Keyframe Relationships

```
Stage
 ©¸©¤ Clip 0 (ID: 0)
     ©¸©¤ TimelineLayer (name: "Clip_0")
         ©¸©¤ Timeline 0 (120 frames)
             ©À©¤ Keyframe 0 (frame 0)
             ©¦   ©À©¤ Child DisplayObject ID: 10
             ©¦   ©¸©¤ Child DisplayObject ID: 11
             ©À©¤ Keyframe 1 (frame 30)
             ©¦   ©À©¤ Child DisplayObject ID: 12
             ©¦   ©¸©¤ Child DisplayObject ID: 13
             ©¸©¤ Keyframe 2 (frame 60)
                 ©¸©¤ Child DisplayObject ID: 14

 ©¸©¤ Clip 1 (ID: 1)
     ©¸©¤ TimelineLayer (name: "Clip_1")
         ©¸©¤ Timeline 1 (120 frames)
             ©À©¤ Keyframe 3 (frame 0)
             ©¦   ©¸©¤ Child DisplayObject ID: 20
             ©¸©¤ Keyframe 4 (frame 45)
                 ©À©¤ Child DisplayObject ID: 21
                 ©¸©¤ Child DisplayObject ID: 22

Global TimelineState:
- current_frame: 45
- is_playing: true
```

## Interaction Flow

```
User clicks on timeline
         ¡ý
        ImGui mouse position
         ¡ý
TimelineUI::handleMouseInput(mouse_pos)
         ¡ý
    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
    ©¦ Determine clicked area:          ©¦
    ©¦ - Frame grid? ¡ú calculate frame  ©¦
    ©¦ - Layer label? ¡ú select layer    ©¦
    ©¦ - Keyframe? ¡ú future extension   ©¦
    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
         ¡ý
    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
    ©¦ Update TimelineState:            ©¦
    ©¦ state.current_frame = clicked_f  ©¦
    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
         ¡ý
    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
    ©¦ Next render cycle:               ©¦
    ©¦ - Playhead redraws at new pos    ©¦
    ©¦ - Display children for new frame ©¦
    ©¦ - UI updates reflected           ©¦
    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
         ¡ý
    ? Interaction complete
```

## Timeline Panel Layout (Pixel Example)

```
Window width: 1600px, height: 200px

©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
©¦  0      5     10     15     20     25     30                    ©¦ 20px
©À©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
©¦ Clip_0 ©¦¨€    ©¦ ¨„    ©¦     ©¦     ©¦     ©¦     ©¦                 ©¦ 40px
©¦        ©¦     ©¦      ©¦     ©¦     ©¦     ©¦     ©¦                 ©¦
©À©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
©¦ Clip_1 ©¦     ©¦      ©¦ ¨€   ©¦ ¨„   ©¦     ©¦     ©¦                 ©¦ 40px
©¦        ©¦     ©¦      ©¦     ©¦     ©¦     ©¦     ©¦                 ©¦
©À©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
©¦ Clip_2 ©¦     ©¦      ©¦     ©¦     ©¦ ¨€   ©¦     ©¦                 ©¦ 40px
©¦        ©¦     ©¦      ©¦     ©¦     ©¦     ©¦     ©¦                 ©¦
©À©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©È
©¦ Clip_3 ©¦     ©¦      ©¦     ©¦     ©¦     ©¦ ¨€   ©¦                 ©¦ 40px
©¦        ©¦     ©¦      ©¦     ©¦     ©¦     ©¦     ©¦                 ©¦
©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
 ©¸©¤120px©¤©¤©¼©¤15px per frame©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼

Legend:
¨€ = Keyframe with data
¨„ = Keyframe continuation
| = Frame grid lines
0-30 = Frame numbers
Clip_N = Layer labels
```

## Performance Comparison Graph

```
Memory Usage vs Keyframe Count

Before:  Linear growth with frames
         Memory
         ^
         ©¦      ¨u©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
         ©¦     ¨u  120 frames ¡Á 4KB
         ©¦    ¨u   = 480KB
         ©¦   ¨u
         ©¦  ¨u
         ©À©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤¡ú Frames
         0    30    60    90   120

After:   Constant growth with keyframes
         Memory
         ^
         ©¦                ©¤©¤©¤©¤©¤©¤©¤
         ©¦              ¨u  3 keyframes ¡Á 4KB
         ©¦            ¨u   = 12KB
         ©¦          ¨u
         ©¦        ¨u
         ©À©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤¡ú Keyframes
         0    1    2    3    4

Savings: ~97% memory reduction!
```

## Timeline State Machine

```
                    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
                    ©¦  Uninitialized  ©¦
                    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¼
                             ©¦
                      .initialize()
                             ©¦
                             ¨‹
                    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
                    ©¦  Initialized    ©¦
                    ©¦ current_frame=0 ©¦
                    ©¦ is_playing=false©¦
                    ©¸©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¼
                         ©¦        ©¦
                   Play  ©¦        ©¦ Seek
                         ©¦        ©¦
                         ¨‹        ¨‹
                    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
                    ©¦  Playing/Paused  ©¦
                    ©¦  Frame updating  ©¦
                    ©¸©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¼
                         ©¦        ©¦
                   Pause ©¦        ©¦ Stop
                         ©¦        ©¦
                         ¨‹        ¨‹
                    ©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
                    ©¦  Paused/Stopped ©¦
                    ©¦ current_frame=X ©¦
                    ©¦ is_playing=false©¦
                    ©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
```

## Class Relationship Diagram

```
DisplayObject (existing)
     ¡ø
     ©¦
     ©¸©¤©¤ DisplayObjectContainer
          ¡ø
          ©¦
          ©À©¤©¤ Stage ?©¤©¤©¤ TimelineSystem
          ©¦
          ©¸©¤©¤ Clip

TimelineSystem ©¤©¤©¤©Ð©¤©¤©¤ KeyframePool
                  ©¦
                  ©À©¤©¤©¤ TimelinePool
                  ©¦    ©¸©¤ Timeline[N]
                  ©¦
                  ©À©¤©¤©¤ TimelineUI
                  ©¦
                  ©À©¤©¤©¤ TimelineLayer[N]
                  ©¦
                  ©¸©¤©¤©¤ TimelineState
```

---

This visual reference should help understand the timeline system's structure and data flow!
