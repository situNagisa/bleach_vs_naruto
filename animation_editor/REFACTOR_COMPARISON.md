# 新旧结构对比 - AS3 显示树重构

## 核心思想差异

### 旧设计（平坦结构）
```
Stage
├── clips[] vector (所有剪辑的数组)
├── stage_root Clip* (虚拟根)
└── current_editing_context Clip* (当前编辑环境)

问题：
- clips 和 stage_root 同时存在，职责不清
- children 指针容易失效（vector 重新分配）
- 嵌套关系只存在指针中，易混乱
- 递归机制不完整，需要手动处理
```

### 新设计（AS3 显示树）
```
Stage (继承 DisplayObjectContainer)
├── children[] (直接包含所有顶层对象)
│   ├── Clip #1 (继承 DisplayObjectContainer)
│   │   └── children[]
│   │       ├── Clip #2 (子剪辑)
│   │       └── Clip #3 (子剪辑)
│   └── Clip #4
├── pan_x, pan_y, zoom (舞台属性)
└── 方法：createClip(), deleteClip(), selectClip()

优点：
- 单一职责：Stage 就是 DisplayObjectContainer
- 树形结构清晰，关系明确
- 自动递归，无需手动处理
- 全部对象在显示树中
```

## 关键方法的演变

### 1. 更新逻辑

**旧代码**：
```cpp
// main.cpp PHASE 1
if (stage.stage_root && stage.stage_root->player) {
    updateClipPlayback(*stage.stage_root);  // 更新虚拟根
}
for (auto& c : stage.clips) {
    if (c.player && !c.parent) {
        updateClipPlayback(c);  // 手动更新顶层
    }
}

// updateClipPlayback() 函数
void updateClipPlayback(Clip& clip) {
    clip.player->update();
    
    for (Clip* child : clip.children) {
        if (child && child->player) {
            child->player->gotoFrame(parent_frame % child->frameCount);
        }
    }
    // 嵌套的子剪辑不会被递归更新！
}
```

**新代码**：
```cpp
// main.cpp PHASE 1
stage.update();  // 就这一句！

// DisplayObjectContainer::update()
virtual void update(int parent_frame = 0) override {
    updateSelf(parent_frame);  // 自己的逻辑
    
    for (auto child : children) {
        child->update(parent_frame);  // 自动递归所有子对象
    }
}

// Clip::updateSelf()
virtual void updateSelf(int parent_frame) override {
    if (parent) {
        Clip* parent_clip = dynamic_cast<Clip*>(parent);
        if (parent_clip && parent_clip->player) {
            int frame = parent_clip->player->currentFrame();
            player->gotoFrame(frame % player->frameCount());
        }
    } else {
        player->update();  // 顶层才自己播放
    }
}
```

改进：
- ? 一句话 `stage.update()` 替代复杂逻辑
- ? 完全递归，支持无限嵌套
- ? 播放同步清晰明确

### 2. 渲染逻辑

**旧代码**：
```cpp
// PHASE 3: 绘制所有剪辑
for (int i = 0; i < (int)stage.clips.size(); ++i) {
    Clip& c = stage.clips[i];
    // ... 渲染逻辑
    ImGui::Image(texture, size);
}
// 子剪辑在 children[] 中但从不被渲染！
```

**新代码**：
```cpp
// PHASE 3: 绘制舞台和所有子对象
ImDrawList* draw_list = ImGui::GetWindowDrawList();
stage.renderStageAxes(draw_list, canvas_pos, canvas_size);
stage.render(draw_list, canvas_pos, canvas_size, pan_x, pan_y, zoom);

// DisplayObjectContainer::render()
virtual void render(...) override {
    renderSelf(...);  // 自己渲染
    
    for (auto child : children) {
        child->render(...);  // 递归渲染所有子对象
    }
}

// Clip::renderSelf()
virtual void renderSelf(...) override {
    // ... 绘制单个剪辑的逻辑
    ImGui::Image(texture, size);
}
```

改进：
- ? 递归自动渲染所有嵌套对象
- ? 清晰分离 renderSelf() 和递归逻辑

### 3. 碰撞检测

**旧代码**：
```cpp
// Stage::getClipAtPosition()
int top_clip = -1;
int top_z = -99999;

for (int i = 0; i < (int)clips.size(); ++i) {
    Clip& c = clips[i];
    // ... 检测逻辑
    if (c.z_order > top_z) {
        top_z = c.z_order;
        top_clip = i;
    }
}
return top_clip;
// 只能检测 clips[] 中的顶层，无法处理嵌套！
```

**新代码**：
```cpp
// Stage::hitTestStage()
float stage_mx = (screen_mx - pan_x) / zoom;
float stage_my = (screen_my - pan_y) / zoom;
return hitTest(stage_mx, stage_my);  // 委托给显示树

// DisplayObjectContainer::hitTest()
int result = -1;

for (int i = (int)children.size() - 1; i >= 0; --i) {  // 从后往前
    int hit = children[i]->hitTest(mx, my);
    if (hit >= 0) return hit;  // 优先最上层
}

result = hitTestSelf(mx, my);
return result;

// Clip::hitTestSelf()
if (我包含这个点) {
    return id;  // 返回自己的ID
}
return -1;
```

改进：
- ? 递归检测所有层级，包括嵌套
- ? 自动从后往前（最上层优先）
- ? 无需 z_order 比较复杂逻辑

### 4. 添加剪辑

**旧代码**：
```cpp
// 拖拽回调
int idx = stage->addClip();
stage->clips[idx].player->load(path);

// Stage::addClip()
Clip c;
c.player = new GifPlayer();
c.id = nextClipId++;
c.z_order = nextZOrder++;
c.parent = current_editing_context;  // 需要手动设置编辑上下文
clips.push_back(c);
int idx = clips.size() - 1;

if (current_editing_context) {
    current_editing_context->children.push_back(&clips[idx]);  // 指针容易失效
}

return idx;
```

**新代码**：
```cpp
// 拖拽回调
Clip* clip = stage->createClip();  // 自动初始化
clip->player->load(path);

// Stage::createClip()
Clip* clip = new Clip();
clip->id = nextClipId++;
clip->z_order = nextZOrder++;
clip->player = new GifPlayer();
addChild(clip);  // DisplayObjectContainer 的方法
return clip;
```

改进：
- ? 返回指针，而不是数组索引
- ? 自动调用 addChild()，管理树关系
- ? 无需 current_editing_context 技巧
- ? 一次创建完全初始化

### 5. 删除剪辑

**旧代码**：
```cpp
// Stage::removeClip(int idx)
Clip& clip_to_remove = clips[idx];

if (clip_to_remove.parent) {
    auto& children = clip_to_remove.parent->children;
    children.erase(std::remove(children.begin(), children.end(), &clip_to_remove), children.end());
}

delete clips[idx].player;
clips.erase(clips.begin() + idx);  // vector 重新分配，所有指针失效！

// 需要修复所有其他指针...
```

**新代码**：
```cpp
// Stage::deleteClip(int clip_id)
for (auto it = children.begin(); it != children.end(); ++it) {
    DisplayObject* obj = *it;
    if (obj && obj->id == clip_id) {
        delete obj;
        removeChild(obj);  // DisplayObjectContainer 的方法
        break;
    }
}

// DisplayObjectContainer::removeChild()
auto it = std::find(children.begin(), children.end(), child);
if (it != children.end()) {
    children.erase(it);
    child->parent = nullptr;
}
```

改进：
- ? 删除时自动管理树关系
- ? 指针管理更清晰
- ? ID 而非数组索引

## 对于你发现的问题的修复

### 问题 1：影片剪辑无法播放
**原因**：`updateClipPlayback()` 不递归处理所有嵌套

**修复**：新代码的 `DisplayObjectContainer::update()` 自动递归
```cpp
stage.update();  // 自动递归更新所有子对象，包括嵌套
```

### 问题 2：时间轴失效
**原因**：`stage_root` 虽然存在但从未真正工作

**修复**：新代码中 Stage 就是 DisplayObjectContainer，自动工作
```cpp
// Clip::updateSelf() 中
if (parent) {  // 如果有父对象
    Clip* p = dynamic_cast<Clip*>(parent);
    player->gotoFrame(p->player->currentFrame() % frameCount);
}
```

### 问题 3：添加子剪辑报错
**原因**：`parent->children.push_back(&clips[idx])` 中的指针在 vector 重新分配时失效

**修复**：新代码显式管理树，不依赖动态数组指针
```cpp
Clip* parent = ...;
Clip* child = new Clip();
parent->addChild(child);  // 直接指针，不涉及 vector 重新分配
```

### 问题 4：子剪辑无法显示
**原因**：渲染逻辑仅遍历 `stage.clips[]`

**修复**：新代码递归渲染
```cpp
stage.render(...);  // 自动递归渲染所有嵌套子对象
```

### 问题 5：交互混乱
**原因**：`getClipAtPosition()` 平坦检测

**修复**：新代码递归碰撞检测
```cpp
int hit = stage.hitTestStage(canvas_mx, canvas_my);  // 自动递归找最上层
```

## 总结：为什么新设计更好

| 指标 | 旧设计 | 新设计 |
|------|--------|--------|
| 结构清晰度 | ?? 混乱 | ????? 树形一致 |
| 递归完整性 | ?? 不完整 | ????? 自动递归 |
| 代码复杂度 | ?? 很多手动逻辑 | ????? 简洁统一 |
| 指针安全性 | ?? 易失效 | ???? 更安全 |
| 可扩展性 | ?? 难以扩展 | ????? 易扩展 |
| 嵌套支持 | ?? 有限 | ????? 无限嵌套 |

## 下一步工作建议

1. **测试播放**：拖入GIF，验证播放是否正常
2. **测试嵌套**：添加子剪辑，验证同步是否有效
3. **测试交互**：拖动、选择、编辑框架
4. **性能优化**：如果需要，可添加脏标志（dirty flags）优化更新
