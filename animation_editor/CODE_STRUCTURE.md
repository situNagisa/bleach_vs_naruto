# 代码结构说明文档 - AS3 显示树结构

## 总体架构

代码采用 **ActionScript 3.0 的显示树结构**，分为 3 个主要部分：

```
DATA STRUCTURES
├── Frame (帧数据)

GIFPLAYER
├── 加载、播放、帧管理

DISPLAY TREE (核心架构)
├── DisplayObject (基类，所有可显示对象)
│   ├── update()       - 更新逻辑（虚方法）
│   ├── render()       - 渲染逻辑（虚方法）
│   ├── hitTest()      - 碰撞检测（虚方法）
│   └── 基础属性：id, x, y, scaleX, scaleY, z_order, parent, selected
│
├── DisplayObjectContainer (容器，继承 DisplayObject)
│   ├── children[]     - 子对象列表
│   ├── addChild()     - 添加子对象
│   ├── removeChild()  - 移除子对象
│   ├── update()       - 递归更新所有子对象
│   ├── render()       - 递归渲染所有子对象
│   ├── hitTest()      - 递归碰撞检测
│   └── 虚方法：updateSelf(), renderSelf(), hitTestSelf()
│
├── Clip (影片剪辑，继承 DisplayObjectContainer)
│   ├── player        - GifPlayer 实例
│   ├── editing_frames - 帧编辑模式
│   └── 覆盖虚方法实现具体逻辑
│
└── Stage (舞台，继承 DisplayObjectContainer，显示树根)
    ├── pan_x, pan_y  - 舞台平移
    ├── zoom          - 舞台缩放
    ├── createClip()  - 创建剪辑
    ├── deleteClip()  - 删除剪辑
    ├── selectClip()  - 选择剪辑
    ├── 层级操作      - raiseClip, lowerClip, raiseClipToTop, lowerClipToBottom
    └── 坐标轴渲染    - renderStageAxes()

MAIN LOOP
├── PHASE 1: UPDATE - 更新显示树
├── PHASE 2: IMGUI FRAME - ImGui帧准备
├── PHASE 3: CANVAS RENDERING - Canvas渲染（含交互）
├── PHASE 4: TIMELINE UI - Timeline面板
└── PHASE 5: RENDERING - 最终渲染
```

## 关键改进说明

### 1. 继承结构 vs 之前的平坦结构

**之前**：
```cpp
struct Clip {
    std::vector<Clip*> children;  // 指针容器，易失效
    Clip* parent;
    // ...数据和方法混杂
};
```

**现在**：
```cpp
class DisplayObject {  // 基类：定义接口
    virtual void update() = 0;
    virtual void render() = 0;
    virtual int hitTest() = 0;
};

class DisplayObjectContainer : public DisplayObject {  // 容器：递归逻辑
    std::vector<DisplayObject*> children;
    virtual void update() override {  // 自动递归更新子对象
        updateSelf();
        for (auto child : children)
            child->update();
    }
};

class Clip : public DisplayObjectContainer {  // 具体实现
    void updateSelf() { /* 自己的逻辑 */ }
};
```

### 2. 递归机制的完整性

所有三个核心方法都自动递归：

```
update()
  ├─ updateSelf()           // 自己的逻辑
  └─ for child in children  // 递归更新所有子对象
      └─ child->update()

render()
  ├─ renderSelf()           // 自己的渲染
  └─ for child in children  // 递归渲染所有子对象
      └─ child->render()

hitTest(mx, my)
  ├─ for i=(children.size-1) downto 0  // 从后往前（最上层优先）
  │   └─ child->hitTest()
  └─ hitTestSelf()          // 最后检测自己
```

### 3. 播放同步现在完全正确

```cpp
// Clip::updateSelf()
if (parent) {
    Clip* parent_clip = dynamic_cast<Clip*>(parent);
    if (parent_clip && parent_clip->player) {
        int parent_frame = parent_clip->player->currentFrame();
        player->gotoFrame(parent_frame % frameCount);  // 同步到父帧
    }
}
```

这样：
- **舞台** → 自动播放
- **顶层剪辑** → 同步舞台帧
- **子剪辑** → 同步父剪辑帧
- **嵌套无限层** → 自动递归同步

### 4. 碰撞检测从后往前

```cpp
// DisplayObjectContainer::hitTest()
for (int i = (int)children.size() - 1; i >= 0; --i) {  // 从最后开始
    int hit = children[i]->hitTest(mx, my);
    if (hit >= 0) return hit;  // 优先返回最上层
}
```

确保最上面的剪辑优先响应。

## 使用示例

### 添加剪辑
```cpp
Clip* clip = stage.createClip();  // 自动赋予 ID 和 z_order
clip->player->load("animation.gif");
stage.selectClip(clip->id);
```

### 嵌套结构
```cpp
Clip* parent = stage.createClip();
parent->player->load("parent.gif");

Clip* child = new Clip();
child->id = stage.nextClipId++;
child->z_order = stage.nextZOrder++;
child->player = new GifPlayer();
parent->addChild(child);  // 加入显示树
```

播放时会自动同步：
```
parent.update()
  ├─ parent.player->update()           // parent 播放
  └─ parent->updateSelf()
      └─ child->update()
          └─ child->updateSelf()       // child 同步到 parent 的帧
```

### 删除剪辑
```cpp
stage.deleteClip(clip_id);  // 自动清理 children 关系
```

### 碰撞检测
```cpp
// Canvas 中
int hovered_id = stage.hitTestStage(canvas_mx, canvas_my);
if (hovered_id >= 0) {
    // 找到的是最上层的剪辑
}
```

## 与之前代码的主要区别

| 方面 | 之前 | 现在 |
|------|------|------|
| 结构 | 平坦，所有剪辑在 `stage.clips[]` | 树形，通过 parent/children 关系 |
| 更新 | 手动处理舞台、顶层、子对象 | 自动递归，调用 `stage.update()` |
| 渲染 | 遍历 `stage.clips[]` 渲染 | 递归调用 `stage.render()` |
| 碰撞检测 | 平坦遍历 | 递归，优先最上层 |
| 添加子对象 | 修改 `current_editing_context` | 直接 `parent->addChild(child)` |
| 删除 | 需要手动更新指针 | 自动处理 parent/children 关系 |
| 层级操作 | 修改 `z_order` | 修改 `z_order` 或调用容器方法 |

## 当前完整的功能

? **已完成**：
- DisplayObject/DisplayObjectContainer/Clip/Stage 层级体系
- 递归更新、渲染、碰撞检测
- 完整的播放同步机制
- 坐标轴和网格
- 帧编辑模式
- 双击进入编辑
- 拖动、缩放、层级操作

?? **需要验证**：
- 播放是否正常工作
- 时间轴同步是否有效
- 添加/删除剪辑时是否稳定

## 代码阅读建议

1. 从 **DisplayObject** 开始理解虚方法接口
2. 看 **DisplayObjectContainer** 的递归机制
3. 了解 **Clip** 如何覆盖 `updateSelf()` 实现播放同步
4. 理解 **Stage** 如何作为显示树的根
5. 在 **MAIN LOOP** 中看完整流程
