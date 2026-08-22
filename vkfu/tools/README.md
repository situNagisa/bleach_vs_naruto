# vkfu_gen —— vk.xml → vkfu 生成器

把 vk.xml 转成 `vkfu/include/vkfu/generated/vulkan-v<version>.h`，内容是 vkfu 框架
（`vulkan_object.h` / `storage.h` / `expression.h` / `branch_pipe.h`）所需的生成层：
`vkfu::obj::*` 标签、`vulkan_object_trait` 特化、`is_vulkan_object_compatible_with_v`
边、`vkfu::param::*` 参数结构体，以及原生 Vulkan 结构体的 `address`/`set_next` 胶水。

## 为什么是两段式

命名这件事在 vk.xml 里没有答案：`VkPhysicalDeviceTimelineSemaphoreFeatures` 该叫什么
是人的判断；而连"把标识符转成 snake_case"本身都不可算法化（`VkPhysicalDeviceIDProperties`、
`deviceLUIDValid`、`clustercullingShader` 各自需要不同的词边界判断）。任何
"算法 + 词表"最后都会长成"算法 + 一堆逐条例外"。

所以判断被全部挤到一个数据文件里：

```
vk.xml ──(纯机械)──> IR（键全是 Vulkan 原标识符）──┐
                                                   ├──> 生成的头文件
                    naming.toml（人写，签入）───────┘
```

- **`ir.py`** 只做 vk.xml 有确定答案的事：结构体/成员/类型、`sType` 取值、
  `structextends`、`allowduplicate`、`returnedonly`、`len` 折叠、定长数组、
  `bitpos`/`value`、`api="vulkan"` 过滤、别名、`<tags>`、平台与 provisional 守卫、
  `vkCreate*` 的 create-info。它不发明任何名字。
- **`naming.toml`** 是所有生成名字的唯一权威。**没有算法，所以没有例外。**
  表里缺条目 → 生成失败，绝不猜。名字想多短都行，因为没有东西去推导它。
- **`naming.py` 的建议算法（含词典）只产出复核材料**，`gen` 从不读它。

## 作用域

只做**构造侧**：root = `vkCreate*` 命令的 create-info 参数（判定规则无需判断：
const 指针参数且其结构体有 `sType` 成员——`pAllocator` 因此自动被排除），
再对 `structextends` 求不动点闭包。

v1.4.328 下是 70 个 root / 440 个对象 / 40 个 branch / 659 条 pNext 边，全部已命名。
查询侧（`returnedonly` + `vkGet*Properties2`）不在此范围内。

## 命名空间：共享前缀不进名字

440 个对象按 Vulkan 名字的族分三层，共享前缀由命名空间承担而不是抄进每个名字里：

| 命名空间 | 数量 | 来源 | 例 |
| --- | --- | --- | --- |
| `param::` | 70 | `vkCreate*` 的 create-info，也就是 root | `device` `image` `swapchain` `graphics_pipeline` `render_pass` / `render_pass2` |
| `param::feature::` | 235 | `VkPhysicalDevice*Features*` | `timeline_semaphore` `mesh_shader_ext` `core`(= `VkPhysicalDeviceFeatures2`) |
| `param::option::` | 135 | 其余挂在 pNext 上的节点 | `device_private_data` `image_format_list` `ycbcr_conversion` |

`obj::` 与 `param::` 用同一套限定名，所以 `obj::feature::timeline_semaphore` 与
`param::feature::timeline_semaphore` 一一对应。命名空间隔开之后
`feature::pipeline_robustness`（能力开关）与 `option::pipeline_robustness`
（单条管线的设置）可以同名共存，这本来就是同一概念的两个角色。

**撞名只在需要时才加厂商标记**，一共 10 个对象：`acceleration_structure_khr` /
`_nv`、`mesh_shader_ext` / `_nv`、`external_format_android` / `_qnx` 等。
其中一边是核心版（无标记）时，核心版保留裸名，因为它没有标记可加。

## 字段名相对于对象

字段不再复述对象自己的名字：

```cpp
feature::timeline_semaphore{.enable = true}                       // 而不是 .timeline_semaphore
feature::buffer_device_address{.enable = 1, .capture_replay = 1}  // 而不是 .buffer_device_address_capture_replay
feature::tile_shading{.enable = 1, .per_tile_draw = 1}            // 剥掉 14 个字段共有的 tile_shading_ 前缀
extended_dynamic_state3{.alpha_to_coverage_enable = 1}            // 剥掉 30 多个字段共有的前缀
```

规则两条：字段名恰好等于对象叶名且是个开关 → 叫 `enable`（v1.4.328 下 166 个字段）；
字段名以"对象叶名 + `_`"开头 → 剥掉这段前缀。剥完以数字开头的不剥（`layout8_bit_access`），
留给人处理。字段名等于叶名但**不是**开关的会打 `restates` 标记要求人工命名——
叫 `enable` 会是撒谎。剥离后平均字段名 16.3 个字符。

## 用法

```bash
python -m vkfu_gen dump-ir  --xml vk.xml --out ir.json
python -m vkfu_gen suggest  --xml vk.xml --table naming.toml --out naming.suggested.toml
python -m vkfu_gen promote  --xml vk.xml --table naming.toml --out naming.suggested.toml
python -m vkfu_gen gen      --xml vk.xml --table naming.toml --scope closure \
    --out ../include/vkfu/generated/vulkan-v1.4.328.h
```

`suggest` 只列出表里还没有的条目；复核并就地改完后 `promote` 把它折进 `naming.toml`
（已有条目一律不动）。此后 `naming.toml` 就是手工维护的文件。

`--scope closure` 要求闭包里每个对象都有名字，缺一个就失败。`--scope table`（默认）
只生成命名过的，并报告还差多少，适合按用到才填表的增量节奏。

`gen` 在这些情况下**硬失败**：缺名字、名字撞车、同一结构体内字段名撞车、
遇到未支持的成员形态（多维数组、位无法一位一格的 flag 枚举）。
**只警告**：字段名与所属对象同名、表里命名了不在作用域内的标识符。

## flags 的处理

flag 成员展开成一个**布局与原生 flags 字完全一致**的位域结构体，直接 `bit_cast`：

```cpp
struct flags_type
{
    VkCommandPoolCreateFlags transient : 1 = 0;
    VkCommandPoolCreateFlags reset_command_buffer : 1 = 0;
    VkCommandPoolCreateFlags protected_ : 1 = 0;
    VkCommandPoolCreateFlags _reserved_3 : 29 = 0;
};

static_assert(sizeof(flags_type) == sizeof(VkCommandPoolCreateFlags));
static_assert(::std::bit_cast<VkCommandPoolCreateFlags>(flags_type{.transient = 1}) == VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
// ...
.flags = ::std::bit_cast<VkCommandPoolCreateFlags>(flags),
```

几个要点：

- 位域类型用成员自己的 flags 类型（`VkFlags` 或 `VkFlags64`），空位和尾部都显式补
  `_reserved_N`，把整个字铺满——否则位域分配单元里会留下未定值，`bit_cast` 在常量
  表达式里就不可用了。
- **位域分配顺序是实现定义的**，所以不假设，而是逐位 `static_assert` 对着真正的
  `VK_*_BIT` 验一遍。换编译器/ABI 出错的话是编译期报错，不是静默错值。
- 位域按 `bitpos` 定位而**不引用** `VK_*_BIT`，所以平台受限的位照样占位，
  布局不会随平台漂移；只有那条 `static_assert` 需要 `#if` 包起来。
- 用 `value` 而非 `bitpos` 定义的条目被排除：v1.4.328 里是 8 个零哨兵
  （`..._DEFAULT` / `_UNKNOWN` / `_INVALID`，语义就是"没有任何位"）和 2 个复合别名
  （`VK_SHADER_STAGE_ALL_GRAPHICS` / `_ALL`，逐位设置即可表达）。排除后
  57 个 flag 枚举全部可 `bit_cast`。

## 建议算法与词典

分词是两段式：先按 camelCase 切成原子（数字单独成原子），再由词典做合并/拆分，
最后落单的纯数字回贴到前一个词（所以 `Features2` → `features2`，而 `Of3D` → `of` + `3d`）。

词典（`naming.py` 的 `LEXICON`）里每一条都是被 v1.4.328 的真实标识符逼出来的：

| 类别 | 条目 | 修的是 |
| --- | --- | --- |
| 合并 | `1d` `2d` `3d` | `Image2DViewOf3D` → `image2_d_view_of3_d` |
| 合并 | `macos` | `MacOSSurface` → `mac_os_surface` |
| 合并 | `ycbcr` | `WithoutYCbCrSampler` → `y_cb_cr` |
| 合并 | `rgba10x6` | `RGBA10X6Formats` → `rgba10_x6` |
| 合并 | `bfloat16` | `shaderBFloat16Type` → `shader_b_float16_type` |
| 合并 | `a4b4g4r4` `a4r4g4b4` | `formatA4B4G4R4` → `format_a4_b4_g4_r4` |
| 拆分 | `astc` `hdr` | `TextureCompressionASTCHDR` → `astchdr` |
| 拆分 | `cluster` `culling` | `clustercullingShader` → `clusterculling_shader` |
| 合并 | `directfb` | `DirectFBSurface` → `direct_fb_surface`（`FB` 恰好也是真厂商标记，所以启发式救不了） |
| 合并 | `imagepipe` | `ImagePipeSurface` → `image_pipe_surface` |
| 词汇 | `av1` `io` `ios` `pps` `rdma` `sm` `sps` `vp9` `vps` | 真缩写，让 `acronym` 标记不被刷屏 |
| 词汇 | 22 个长单词（`rasterization` `maintenance` …） | 让 `wordrun` 标记不被刷屏 |

复核标记（只出现在 `naming.suggested.toml` 里）：

| tag | 含义 |
| --- | --- |
| `restates` | 字段名复述了对象名，但它不是开关，`enable` 不适用 |
| `collision` | 裸名被争用（有个带厂商标记的近亲），确认该由谁占裸名 |
| `acronym` | 有连写大写且词典不认识，词的切分是猜的；vk.xml `<tags>` 声明过的厂商标记、以及被词典消化掉的原子都不算 |
| `lone` | 切出了孤立单字母词，典型是 `Dimension2D` → `dimension2_d` |
| `wordrun` | 长词里没有边界，且词典解释不了（尾部版本号不算词的一部分） |
| `keyword` | 撞 C++ 关键字，已加尾部 `_` |
| `prefix` | 枚举常量不以其 flag 类型的前缀开头 |
| `invalid` | 结果不是合法标识符（以数字开头） |

词典和标记规则调过之后，v1.4.328 的约 1880 条建议里只剩 **36 条**带 REVIEW，
全部需要人做语义判断，已逐条定案（见下）。

## 本次人工校对的结论

- **12 条 `invalid`**（常量以数字开头），按各枚举自身语义补限定词：
  `VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT` → `view_2d_array_compatible`（3D 图像可建
  2D array 视图）、`..._2D_VIEW_COMPATIBLE_BIT_EXT` → `view_2d_compatible`（可对单切片
  建 2D 视图）、光流网格 → `size_1x1` … `size_8x8`、色度子采样 → `ratio_420/422/444`
  （与同枚举的 `monochrome` 并列）、分量位深 → `depth_8/10/12`。
- **7 条 `keyword`** 保留尾部下划线：6 条 `PROTECTED_BIT` → `protected_`（含义本来就是
  protected，下划线纯粹是 C++ 关键字转义，换成 `protected_memory` 反而会给命令池那条
  加上它没有的含义），以及剥完前缀后撞上关键字的
  `video_session_parameters{.template_ = ...}`（它就是"以哪个对象为模板"）。
- **8 条 `lone`** 原样接受：`address_mode_u/v/w` 是纹理坐标轴、`present_gravity_x/y`、
  `position_x_only`、`viewport_w_scaling` 的 W 分量、`enable_y_degamma` 的 Y 亮度通道，
  单字母就是原意。
- **4 条叶名以数字开头**：`feature::storage_16bit` / `storage_8bit`（原 `16_bit_storage`）、
  `feature::format_4444`；`VkPhysicalDeviceFeatures2` 剥完只剩版本号 `2`，定为
  `feature::core`——它承载 1.0 核心特性并领着整条特性链。顺带把这三个的字段也按
  语义重排：`feature::storage_16bit{.storage_buffer = 1, .push_constant = 1}`。
- **3 条 `restates`**（复述对象名但不是开关）：`option::semaphore_type{.type = ...}`、
  `option::pipeline_creation_feedback{.pipeline = ..., .stages = ...}`、
  `option::opaque_capture_descriptor_data{.data = ...}`。
- **2 条跨命名空间同叶名**按实际含义区分：`VkPipelineBinaryInfoKHR` →
  `option::pipeline_binaries{.binaries = ...}`（携带一组已建好的 binary，区别于 root
  `pipeline_binary`）、`VkSamplerYcbcrConversionInfo` → `option::ycbcr_conversion`
  （指定用哪个已建的转换，区别于 root `sampler_ycbcr_conversion`）。

## 已知欠账

- 创建期元信息（physical_device / allocator / 池句柄）与 `create_*` 函数还没做；
  框架侧也还没有"非 pNext 节点"这个概念。
- 查询侧未做。
- 本机没有编译器和 Vulkan 头，生成结果尚未编译验证（已做标识符回查与结构自检）。
- 一个 count 被多个指针共用时（如 `VkWriteDescriptorSet`）不折叠成 span，
  保留 count + 裸指针。
- `option::` 里还有几个很长的名字（最长 `option::ray_tracing_pipeline_cluster_acceleration_structure`）。
  它们的前缀多是"挂在谁身上"，调用点其实已经能看出来，进一步按父节点分层可以再短一截——
  但 30 个对象有多个父节点，需要逐个定。
