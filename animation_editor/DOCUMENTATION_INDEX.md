# Timeline System - Complete Documentation Index

Welcome! This is your complete guide to the Adobe Animate CC-style timeline system.

## ?? Documentation Overview

### For Getting Started
- **[ANIMATE_CC_QUICK_START.md](./ANIMATE_CC_QUICK_START.md)** ? START HERE
  - 5-minute tutorial
  - Common tasks
  - Debugging tips
  - Common mistakes

### For Understanding the System
- **[ANIMATE_CC_SEMANTICS.md](./ANIMATE_CC_SEMANTICS.md)** ?? DEEP DIVE
  - Complete architecture
  - Frame evaluation algorithm
  - Frame editing operations
  - Design philosophy
  - Usage patterns
  - Invariants and constraints

### For API Reference
- **[TIMELINE_API_REFERENCE.md](./TIMELINE_API_REFERENCE.md)** ?? COMPLETE API
  - Every class and method
  - Full signatures
  - Parameter descriptions
  - Return values
  - Built-in types

### For Quick Lookup
- **[TIMELINE_CHEAT_SHEET.md](./TIMELINE_CHEAT_SHEET.md)** ? COPY/PASTE CODE
  - Common operations
  - Code snippets
  - Debugging commands
  - Do's and don'ts
  - Troubleshooting table

### Status & Summary
- **[IMPLEMENTATION_COMPLETE.md](./IMPLEMENTATION_COMPLETE.md)** ? PROJECT STATUS
  - What was completed
  - Features implemented
  - Build status
  - Design decisions

---

## ?? Reading Path by Role

### I'm a Designer/Artist
1. **[ANIMATE_CC_QUICK_START.md](./ANIMATE_CC_QUICK_START.md)** - Understand the basics
2. **[TIMELINE_CHEAT_SHEET.md](./TIMELINE_CHEAT_SHEET.md)** - Copy common patterns

### I'm a Programmer Implementing This
1. **[ANIMATE_CC_QUICK_START.md](./ANIMATE_CC_QUICK_START.md)** - 5-minute overview
2. **[ANIMATE_CC_SEMANTICS.md](./ANIMATE_CC_SEMANTICS.md)** - Understand design
3. **[TIMELINE_API_REFERENCE.md](./TIMELINE_API_REFERENCE.md)** - Learn all methods
4. **[TIMELINE_CHEAT_SHEET.md](./TIMELINE_CHEAT_SHEET.md)** - Keep as reference

### I'm Extending/Maintaining This
1. **[ANIMATE_CC_SEMANTICS.md](./ANIMATE_CC_SEMANTICS.md)** - Understand invariants
2. **[TIMELINE_API_REFERENCE.md](./TIMELINE_API_REFERENCE.md)** - Know every API
3. **[IMPLEMENTATION_COMPLETE.md](./IMPLEMENTATION_COMPLETE.md)** - See what's done
4. Code comments in `timeline_data.h` - Implementation details

### I Need to Debug
1. **[TIMELINE_CHEAT_SHEET.md](./TIMELINE_CHEAT_SHEET.md)** - Debugging section
2. **[ANIMATE_CC_SEMANTICS.md](./ANIMATE_CC_SEMANTICS.md)** - Understanding invariants
3. `timeline_data.h` - Check implementations

---

## ?? Key Concepts (TL;DR)

### The Model
- **Timeline** = sequence of frames with content
- **Keyframe** = content at a specific frame
- **FrameSpan** = interval `[start, end)` covered by one keyframe
- **Tween** = interpolation between keyframes

### Frame Semantics
```
Timeline: [=====KF1=====][=====KF2=====][=====KF3=====]
Frames:   [0------30----)[30-----60----)[60-----90----)
```
- Frame `f` is covered by exactly one FrameSpan
- Only keyframe at span start stores data
- Other frames derive values via tween (if present)

### Editing
- **Insert Keyframe** ¡ú Auto-splits span if in middle
- **Delete Keyframe** ¡ú Removes entire FrameSpan
- **Insert Frames** ¡ú Shift everything forward
- **Delete Frames** ¡ú Shift everything backward

### Playing
- **Asset** = immutable animation data
- **Instance** = playback state (frame, playing, mode)
- Multiple instances can play same asset independently

---

## ?? File Structure

```
animation_editor/
©À©¤©¤ timeline_data.h                    ¡û Core data structures
©¦   ©À©¤©¤ TimelineKeyframe              ¡û Single keyframe
©¦   ©À©¤©¤ FrameSpan                     ¡û Interval with keyframe
©¦   ©À©¤©¤ Timeline                      ¡û Frame sequence
©¦   ©À©¤©¤ FrameTween (abstract)         ¡û Interpolation interface
©¦   ©À©¤©¤ LinearTween                   ¡û Linear interpolation
©¦   ©À©¤©¤ EaseInTween                   ¡û Cubic ease-in
©¦   ©À©¤©¤ EaseOutTween                  ¡û Cubic ease-out
©¦   ©À©¤©¤ KeyframePool                  ¡û Keyframe storage
©¦   ©À©¤©¤ TimelinePool                  ¡û Timeline storage
©¦   ©À©¤©¤ Layer                         ¡û Editor metadata
©¦   ©À©¤©¤ TimelineAsset                 ¡û Animation data container
©¦   ©À©¤©¤ TimelineInstance              ¡û Playback state
©¦   ©¸©¤©¤ TimelineState                 ¡û Editor state
©À©¤©¤ timeline_layer.h                   ¡û UI layer rendering
©À©¤©¤ timeline_mgmt.h                    ¡û System management
©¦
©À©¤©¤ DOCUMENTATION/
©À©¤©¤ ANIMATE_CC_QUICK_START.md          ¡û 5-min tutorial ?
©À©¤©¤ ANIMATE_CC_SEMANTICS.md            ¡û Architecture ??
©À©¤©¤ TIMELINE_API_REFERENCE.md          ¡û Full API ??
©À©¤©¤ TIMELINE_CHEAT_SHEET.md            ¡û Copy/paste ?
©À©¤©¤ IMPLEMENTATION_COMPLETE.md         ¡û Status ?
©¸©¤©¤ DOCUMENTATION_INDEX.md             ¡û This file ??
```

---

## ?? Quick Start

```cpp
// 1. Create asset
auto asset = std::make_shared<TimelineAsset>();
asset->setFrameCount(120);

// 2. Create timeline
int timeline_id = asset->createTimeline(120);

// 3. Add keyframes
int kf0 = asset->insertKeyframeAtFrame(timeline_id, 0);
int kf30 = asset->insertKeyframeAtFrame(timeline_id, 30);

// 4. Add objects
asset->getMutableKeyframePool().addChildToKeyframe(kf0, obj_id);

// 5. Create playback instance
auto instance = std::make_unique<TimelineInstance>(asset);
instance->setPlaying(true);

// 6. Render loop
for (int frame = 0; frame < 120; frame++) {
    int kf_id = asset->getKeyframeAtFrame(timeline_id, frame);
    // ... render ...
    instance->advanceFrame();
}
```

**Want more?** ¡ú Read [ANIMATE_CC_QUICK_START.md](./ANIMATE_CC_QUICK_START.md)

---

## ? FAQ

**Q: Why FrameSpans instead of per-frame storage?**
A: Follows Adobe Animate CC model. Keyframes define intervals, not individual frames. More efficient and aligns with animation authoring.

**Q: Can I have multiple playback speeds?**
A: Yes! Each `TimelineInstance` tracks its own playback. Set different `frame_duration_ms` per instance.

**Q: How do I add custom tweens?**
A: Subclass `FrameTween` and implement `evaluate()`. See TIMELINE_API_REFERENCE.md for example.

**Q: Can I have nested timelines?**
A: Not in this version. Can be added as future enhancement. For now, use multiple independent timelines.

**Q: How do I save/load animations?**
A: Serialize `TimelineAsset` structure. Not included in this version. Can be added.

**Q: Is this thread-safe?**
A: No. Use one thread for editing, separate for playback. Or add locks.

---

## ?? Common Tasks

### Play an Animation
See: [ANIMATE_CC_QUICK_START.md](./ANIMATE_CC_QUICK_START.md) - Step 5

### Edit Timeline
See: [ANIMATE_CC_SEMANTICS.md](./ANIMATE_CC_SEMANTICS.md) - Frame Editing Operations

### Query Frame Data
See: [TIMELINE_CHEAT_SHEET.md](./TIMELINE_CHEAT_SHEET.md) - Frame Evaluation

### Debug Issues
See: [TIMELINE_CHEAT_SHEET.md](./TIMELINE_CHEAT_SHEET.md) - Debugging section

### Extend System
See: [ANIMATE_CC_SEMANTICS.md](./ANIMATE_CC_SEMANTICS.md) - Future Enhancements

---

## ?? Getting Help

1. **Error in code?** ¡ú Check [TIMELINE_CHEAT_SHEET.md](./TIMELINE_CHEAT_SHEET.md) Troubleshooting
2. **Don't understand FrameSpans?** ¡ú Read [ANIMATE_CC_SEMANTICS.md](./ANIMATE_CC_SEMANTICS.md) Architecture
3. **Need specific API?** ¡ú Search [TIMELINE_API_REFERENCE.md](./TIMELINE_API_REFERENCE.md)
4. **Want code example?** ¡ú Copy from [TIMELINE_CHEAT_SHEET.md](./TIMELINE_CHEAT_SHEET.md)

---

## ? Implementation Status

- ? Core data structures
- ? Frame evaluation semantics
- ? Frame editing operations
- ? Tween system
- ? Asset/Instance separation
- ? Layer management
- ? Complete API
- ? Comprehensive documentation
- ? Code compiles (no errors)

**Not included (future enhancements):**
- Serialization (save/load)
- Undo/redo system
- Shape tweening
- Nested timelines
- Frame events
- Custom curves

See [IMPLEMENTATION_COMPLETE.md](./IMPLEMENTATION_COMPLETE.md) for details.

---

## ?? Design Principles

This system follows these principles:

1. **Correctness** - Strictly adheres to Animate CC semantics
2. **Clarity** - Code and documentation are clear and explicit
3. **Efficiency** - O(1) lookups, O(n) edits for n spans
4. **Extensibility** - Easy to add custom tweens, serialization, etc.
5. **Separation of Concerns** - Data (Asset) vs Runtime (Instance) separated
6. **Strong Invariants** - All FrameSpan operations maintain validity

---

## ?? Learning Resources

### Video Concepts (Mental Model)
1. **Discrete Frame Model** - Think: index-based, not time-based
2. **FrameSpans** - Think: intervals, not individual frames
3. **Keyframes at Starts** - Think: Keyframe = FrameSpan start position
4. **Tweens as Interpolation** - Think: value deriving, not key storing

### Code Concepts
1. **Pool Pattern** - Objects stored centrally, referenced by ID
2. **Move Semantics** - Tweens use `std::unique_ptr` and move
3. **Const Correctness** - Read operations take const references
4. **RAII** - All resources automatically managed

---

## ?? Next Steps

1. **Read** [ANIMATE_CC_QUICK_START.md](./ANIMATE_CC_QUICK_START.md)
2. **Understand** [ANIMATE_CC_SEMANTICS.md](./ANIMATE_CC_SEMANTICS.md)
3. **Reference** [TIMELINE_API_REFERENCE.md](./TIMELINE_API_REFERENCE.md)
4. **Implement** - Use [TIMELINE_CHEAT_SHEET.md](./TIMELINE_CHEAT_SHEET.md)
5. **Extend** - Add serialization, undo/redo, custom tweens

---

**Happy animating! ??**

For questions or issues, check the documentation index above.
