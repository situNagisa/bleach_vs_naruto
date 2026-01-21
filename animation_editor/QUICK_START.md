# 快速开始 - 新代码使用指南

## 编译和运行

```bash
编译: ? 已成功编译
运行: 启动程序后拖入 GIF 或图片文件
```

## 基本操作

### 加载媒体
1. 拖拽 GIF 或图片文件到窗口 → 自动创建剪辑
2. Timeline 面板会显示剪辑信息

### 播放控制
- **Play/Pause** 按钮：播放/暂停动画
- **Frame 滑块**：切换帧
- **信息显示**：当前帧/总帧数、持续时间、缩放比例

### 舞台操作
- **左键拖动**：在空白处拖动 → 移动舞台视图
- **滚轮**：缩放舞台（0.1x ~ 10x）
- **坐标轴**：彩色网格和坐标轴指示舞台位置

### 剪辑操作
- **左键点击**：选择剪辑（绿色边框）
- **左键拖动**：移动选中的剪辑
- **双击**：进入帧编辑模式（蓝色边框）
- **Delete Clip**：删除选中剪辑
- **Raise/Lower**：层级上下移动
- **To Top/To Bottom**：移到最顶/最底

### 帧编辑
1. 双击剪辑 → 进入帧编辑模式（蓝色边框 + [EDIT MODE]）
2. 调整 **Offset X/Y** 滑块 → 修改当前帧的位置偏移
3. **Reset** 按钮 → 重置偏移为 (0, 0)
4. 切换帧，每帧的偏移独立保存
5. 双击再次 → 退出编辑模式

## 代码结构速览

### 核心类

**DisplayObject** (基类)
```cpp
虚方法：
- update(parent_frame) - 更新逻辑
- render(...) - 渲染逻辑
- hitTest(mx, my) - 碰撞检测

属性：
- id, x, y, scaleX, scaleY, z_order, parent, selected
```

**DisplayObjectContainer** (容器)
```cpp
方法：
- addChild(child) - 添加子对象
- removeChild(child) - 移除子对象
- numChildren() - 子对象数量
- update() 自动递归更新
- render() 自动递归渲染
- hitTest() 自动递归检测

子类应覆盖：updateSelf(), renderSelf(), hitTestSelf()
```

**Clip** (影片剪辑)
```cpp
继承：DisplayObjectContainer
属性：
- player: GifPlayer* - 播放器
- editing_frames - 帧编辑模式
- editing_frame_idx - 编辑的帧

主要逻辑在 updateSelf()：
- 如果有父剪辑，同步到父帧
- 否则自己播放
```

**Stage** (舞台)
```cpp
继承：DisplayObjectContainer
属性：
- pan_x, pan_y, zoom - 舞台视图参数
- nextClipId, nextZOrder - ID分配

方法：
- createClip() - 创建新剪辑
- deleteClip(id) - 删除剪辑
- selectClip(id) - 选择剪辑
- hitTestStage(x, y) - 碰撞检测
- raiseClip(), lowerClip() 等层级操作
- renderStageAxes() - 绘制坐标轴
```

**GifPlayer** (播放器)
```cpp
方法：
- load(path) - 加载GIF或图片
- update() - 更新播放状态
- setPlaying(bool) - 播放/暂停
- gotoFrame(idx) - 跳转帧
- getCurrentFrame() - 获取当前帧
- setFrameOffset() - 设置帧偏移
```

### 主循环流程

```
PHASE 1: UPDATE
└─ stage.update()  // 递归更新所有对象

PHASE 2: IMGUI FRAME
└─ 准备 ImGui 框架

PHASE 3: CANVAS RENDERING
├─ 处理交互（拖动、点击、缩放）
├─ 绘制坐标轴：stage.renderStageAxes()
└─ 绘制所有对象：stage.render()

PHASE 4: TIMELINE UI
└─ 显示控制面板

PHASE 5: RENDERING
└─ OpenGL 最终渲染
```

## 常见需求

### Q1: 如何添加子剪辑？

```cpp
// 创建父剪辑
Clip* parent = stage.createClip();
parent->player->load("parent.gif");

// 创建子剪辑
Clip* child = new Clip();
child->id = stage.nextClipId++;
child->z_order = stage.nextZOrder++;
child->player = new GifPlayer();
parent->addChild(child);
child->player->load("child.gif");
```

播放时 child 会自动同步到 parent 的帧！

### Q2: 如何删除剪辑？

```cpp
stage.deleteClip(clip_id);  // 自动处理所有关系
```

### Q3: 如何获取选中的剪辑？

```cpp
Clip* selected = stage.getSelectedClip();
if (selected) {
    selected->player->setPlaying(true);
}
```

### Q4: 如何实现自定义对象类型？

```cpp
class CustomObject : public DisplayObjectContainer {
protected:
    virtual void updateSelf(int parent_frame) override {
        // 自己的更新逻辑
    }
    
    virtual void renderSelf(...) override {
        // 自己的渲染逻辑
    }
    
    virtual int hitTestSelf(float mx, float my) override {
        // 自己的碰撞检测
        return id;  // 返回 id 表示命中
    }
};

// 使用
CustomObject* obj = new CustomObject();
obj->id = stage.nextClipId++;
stage.addChild(obj);
```

## 调试技巧

### 打印显示树
```cpp
void printDisplayTree(DisplayObject* obj, int depth = 0) {
    std::string indent(depth * 2, ' ');
    std::cout << indent << "id=" << obj->id << "\n";
    
    DisplayObjectContainer* container = dynamic_cast<DisplayObjectContainer*>(obj);
    if (container) {
        for (auto child : container->children) {
            printDisplayTree(child, depth + 1);
        }
    }
}

printDisplayTree(&stage);  // 打印整个显示树
```

### 验证树结构
```cpp
// 所有对象都应该在树中
for (auto child : stage.children) {
    assert(child->parent == &stage);
}
```

### 性能分析
```cpp
// 计数对象
int countObjects(DisplayObject* obj) {
    DisplayObjectContainer* container = dynamic_cast<DisplayObjectContainer*>(obj);
    int count = 1;
    if (container) {
        for (auto child : container->children) {
            count += countObjects(child);
        }
    }
    return count;
}

int total = countObjects(&stage);
std::cout << "Total objects: " << total << "\n";
```

## 常见问题 (FAQ)

**Q: 为什么播放不工作？**
A: 检查是否加载了有效的 GIF/图片。查看控制台错误信息。

**Q: 为什么嵌套的子剪辑不显示？**
A: 检查子剪辑的 player 是否加载了文件。确认已调用 `parent->addChild(child)`。

**Q: 为什么删除剪辑后崩溃？**
A: 必须使用 `stage.deleteClip(id)` 而不是手动 delete。

**Q: 如何支持更多嵌套层级？**
A: 架构已支持无限嵌套，直接 `parent->addChild(child)` 即可。

**Q: 性能如何？**
A: 递归设计对 1000+ 对象可能变慢。优化建议：
- 添加脏标志（dirty flags）避免不必要的更新
- 离屏剪辑的递归可以跳过
- 使用空间分割加速碰撞检测

## 关键文件

| 文件 | 说明 |
|------|------|
| `animation_editor/main.cpp` | 主代码 |
| `animation_editor/CODE_STRUCTURE.md` | 架构文档 |
| `animation_editor/REFACTOR_COMPARISON.md` | 新旧对比 |
| `animation_editor/REFACTOR_SUMMARY.md` | 重构总结 |

## 下一步

1. **编译运行**：验证基本功能
2. **测试播放**：加载 GIF 验证播放是否正常
3. **测试嵌套**：创建子剪辑验证时间轴同步
4. **测试交互**：所有交互功能是否正常
5. **性能优化**：如需要，添加脏标志等优化

祝你使用愉快！??
