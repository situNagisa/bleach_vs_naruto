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

只做**构造侧**：root = **任何命令**的 const 指针 info 参数（判定规则无需判断：
const 指针参数且其结构体有 `sType` 成员——`pAllocator` 因此自动被排除）。
不限于 `vkCreate*`：`vkAllocate*`、`vkCmd*`、`vkQueue*`、`vkGet*` 的入参结构体
都是调用方自己填的，形态完全一样。

闭包同时对三种关系求不动点：`structextends`（pNext 子节点）、单个 const 指针指向的
带 `sType` 结构体（**表达式槽**），以及带 `len` 的数组元素类型（有自己的 param，但仍是
native 的 span——span 是借用，父级无法拥有元素）。

v1.4.328 下是 **257 个 root / 834 个链式对象 / 69 个无 sType 的 param / 246 个只读的查询对象 /
377 个命令包装**，外加 41 个表达式槽和 55 个数组元素类型，全部已命名。
`param::command_buffer_begin`、`param::rendering`、`param::dependency`、`param::submit2`、
`param::present`、`param::memory`、`param::image_memory_barrier2` 都在里面，所以命令录制和
内存分配那一侧也不用再手写 `sType`。
查询侧（`returnedonly` + `vkGet*Properties2`）不在此范围内。

## 命名空间：共享前缀不进名字

440 个对象按 Vulkan 名字的族分三层，共享前缀由命名空间承担而不是抄进每个名字里：

| 命名空间 | 来源 | 例 |
| --- | --- | --- |
| `param::` | `vkCreate*` 的 create-info，也就是 root | `device` `image` `swapchain` `graphics_pipeline` `render_pass` / `render_pass2` |
| `param::feature::` | `VkPhysicalDevice*Features*` | `timeline_semaphore` `mesh_shader_ext` `core`(= `VkPhysicalDeviceFeatures2`) |
| `param::state::` | 管线状态（表达式槽的目标及其 pNext 后代中以 `Pipeline` 开头的） | `vertex_input` `viewport` `rasterization` `color_blend` `shader_stage` |
| `param::option::` | 其余挂在 pNext 上的节点 | `device_private_data` `image_format_list` `ycbcr_conversion` |
| `vkfu::enums::` | 被当作字段类型的纯枚举（含被当单值用的 FlagBits） | `format` `primitive_topology` `sample_count` |

**厂商标记是命名空间，不是后缀**。vk.xml `<tags>` 里声明的每个标记都自成一层，放在族之后：
`param::khr::swapchain`、`param::feature::ext::mesh_shader` 与 `param::feature::nv::mesh_shader`、
`param::state::nv::viewport_swizzle`。这样跨厂商根本不会撞名，消歧只剩一档：同一命名空间内
仍然重名时保留动词（`nv::acceleration_structure` 对 `nv::acceleration_structure_create`）。

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

## 表达式槽：指针成员也能接表达式

`VkGraphicsPipelineCreateInfo::pVertexInputState` 这类**单个 const 指针**指向的子结构体，
在 param 里是一个模板参数，可以直接塞一整个表达式；父级的存储拥有求值结果并自己接线：

```cpp
auto storage = vkfu::evaluate(param::graphics_pipeline{
    .vertex_input_state = param::state::vertex_input{...} | param::state::vertex_input_divisor{...},
    .viewport_state = param::state::viewport{.viewport_count = 1, .scissor_count = 1},
    .rasterization_state = param::state::rasterization{.polygon_mode = polygon_mode::fill},
} | param::option::pipeline_rendering{...});
```

- 槽是 `template<::vkfu::reference_expression_for<obj::state::vertex_input> ... = ::vkfu::absent_expression>`，
  所以**类型不匹配的子表达式接不上**，没填的槽 native 指针保持 null。
- 调用点仍是聚合初始化：C++20 的聚合 CTAD 支持指定初始化器且允许跳过成员（g++ / clang 均已验证）。
- 存储是 `reference_storage`，拷贝和移动都会重新把父级指向自己那份子存储。
- 有槽的 param 的 `evaluate()` **不是 const**——子表达式自己的 `evaluate()` 不必是 const。

## naming.toml 是重建出来的

命名规则变一次就要重刷五千行表，所以判断被抽到 `naming.overrides.toml`（约 100 行），
`vkfu_gen rebuild` 把 `naming.toml` 重建成"规则提议 + 人工覆盖"。对象条目只写**叶名**，
命名空间由规则给。改分桶规则从此是一条命令，而不是一次手工大改。

## chain：一次性接完所有 feature

`operator|` 是二元的，`a | b | c` 会堆出两个中间类型，而且每接一个就重新校验一遍。
`vkfu::chain` 一步到位，**类型与折叠完全相同**：

```cpp
static_assert(::std::same_as<
    decltype(vkfu::chain(a, b, c)),
    decltype(a | b | c)>);
```

- 一次看到整个集合，所以重复检查是一遍而不是 n 遍。
- 定制点是 ADL 的 `_vkfu_chain(branch, features...)`，与 `_vkfu_address` 等同一套约定。
- `chain(branch)` 就是 branch 本身，和对空包折叠 `|` 一致。

## 已提升类型的旧名字

162 个类型被核心提升过，vk.xml 把扩展记在**旧名字**上。旧代码按旧名字写，所以两个都发：

```cpp
param::feature::timeline_semaphore        // 核心名，1.2 起
param::feature::khr::timeline_semaphore   // 同一个类型，一个命名空间深
```

别名的名字**进命名表**（`[alias."VkXxxKHR"] name = "..."`），和其它名字一样是人工权威。带
表达式槽的目标（6 个）发的是别名模板，槽的默认值原样带过去。撞名时真结构体赢、别名跳过并
计数——v1.4.328 下有 3 个，全都是别名撞别名（同一个结构体的两种旧拼法）。

## 机械性检查：`vkfu_gen check`

命名表里的低级错误不该等编译器来说。这些全部只靠表和 IR 就能判定，`rebuild` 和 `suggest`
跑完自动执行一次，也可以单独跑：

```bash
python -m vkfu_gen check --xml vk.xml --table naming.toml --scope closure
```

| 类别 | 抓什么 |
|---|---|
| `empty` | 空名字，或者 `feature::` 这种没有叶子的（匿名 struct） |
| `invalid` | 不是 C++ 标识符，比如 `property::2` |
| `keyword` | C++ 关键字 |
| `reserved` | 含 `__` 或 `_大写` 开头——保留给实现，程序是 ill-formed 的 |
| `duplicate` | 两个对象/字段/位/枚举量/命令抢一个名字 |
| `scope` | 字段撞上它自己生成的 `<字段>_type`、`<字段>_expression` 或固定别名 |
| `namespace` | 同一个名字既是类又是命名空间（硬错误，只有两条都在了才显形） |
| `shadow` | 命令名盖住 `vkfu` 里已有的 CPO |
| `missing` / `orphan` | 在范围内却没有条目 / 有条目却不在范围内 |

写完当天就抓到一个真的：`VkPhysicalDeviceProperties2` 按属性族剥下来叶子是 `"2"`，
不能当标识符开头。同一个坑 `VkPhysicalDeviceFeatures2` 早就踩过，当时是拿人工 override
盖掉的——检查器让它变成算法里的一条规则（族名兜底）而不是一条个案。

## 命令包装：不止 create

677 个 vulkan 命令里 **318 个值得包装**，判据是"有东西可换"：吃调用方填的结构体（换成表达式），
或者有指针+长度的参数对（折成一个 span）。其余 293 个既无 info 又无 span（`vkCmdDraw`、
`vkCmdBindPipeline`），包装没有收益，保持原样调用；57 个是 `vkGet*` 的两段式枚举模式，
形态不同，暂不处理。

```cpp
vkfu::cmd_execute_commands(cmd, secondaries);              // count + pointer -> 一个 span
vkfu::cmd_set_viewport(cmd, 0, ::std::span{&viewport, 1u});
vkfu::cmd_pipeline_barrier2(cmd, param::dependency{...});  // 直接吃表达式
vkfu::begin_command_buffer(cmd, param::command_buffer_begin{...});
vkfu::queue_submit2(queue, ::std::span{&submit, 1u}, fence);
```

返回 `VkResult` 的给一对重载（抛 / `std::nothrow_t` 返回 `expected`）；返回 `void` 的只有一个
函数，没有可失败的东西。多个 info 参数各自一个模板参数（`vkCmdNextSubpass2` 有两个）。
没有模板参数的包装是非模板函数，所以标了 `inline`。

**参数名**是 vk.xml 的名字去掉匈牙利前缀后 snake 化。它们不是可调用 API 的一部分
（C++ 没有具名实参），所以是唯一不走命名表的一类名字。

## 读侧：枚举和查询链

写侧的镜像，**不分配**——生成的头里没有 `<vector>`。52 个两段式枚举各给三种形态：

```cpp
auto const count = vkfu::count_physical_devices(instance);        // 先问有多少
auto buffer = ::std::array<VkPhysicalDevice, 8>{};
auto const devices = vkfu::enumerate_physical_devices(instance, buffer);   // 写进你的存储，返回写了多少

vkfu::enumerate_physical_devices(instance, out_iterator);         // ranges::copy 的形状和契约
```

span 形态一次调用就够（`count` 是 in-out），`VK_INCOMPLETE` **不是错误**——它说"你的 span
满了，还有更多"，返回写进去的那一截；想知道有没有截断就先问 `count_*`。元素带 sType 的会
先盖好章。

迭代器形态要 **`contiguous_iterator` 而不是 `output_iterator`**：驱动是通过裸指针写的，所以
必须有指针可给，`back_insert_iterator` 编不过。边界契约和 `std::ranges::copy` 一样——调用方
保证有地方放。`void* pData` 那种不透明块是 `std::span<std::byte>`。

**24 个命令的唯一 out 参数是个能被 pNext 扩展的 sType 结构体**，它们变成查询链——调用方只说
形状，wrapper 负责持有、串 pNext、盖 sType：

```cpp
auto props = vkfu::get_physical_device_properties2<
    obj::property::driver, obj::property::id>(physical_device);

props.head().properties.limits.maxImageDimension2D;
props.get<obj::property::driver>().driverName;
```

约束用的是**写侧那套 structextends 边**：不扩展 `VkPhysicalDeviceProperties2` 的东西编译期
就被拒，重复命名同一个也被拒（那会让链指向自己）。

读侧的字段是 native 名字，这是故意的：查询结果是拿来读的，spec 上写的就是这些名字。所以
`property::*` / `result::*` 只有 tag、trait 和 pNext 钩子，**没有 param**——246 个结构体只进
命名表的对象名，不进 2554 个字段名里。

## 需要开哪些扩展，从表达式里推出来

vk.xml 记了每个类型由谁提供，所以不用记：

```cpp
auto chain = param::device{} | param::feature::core{} | param::feature::ext::mesh_shader{};
constexpr auto needed = vkfu::required_extensions<VK_API_VERSION_1_1>(chain);
// {"VK_EXT_mesh_shader"} —— device 和 feature::core 在 1.1 是 core，不贡献
```

`core` 是把它提升进核心的版本（没提升过就是 0），`names` 是提供它的扩展。**要跟着别名走**：
vk.xml 把 `VK_KHR_create_renderpass2` 记在 `VkAttachmentDescription2KHR` 上而不是核心名上，
不跟别名的话 162 个已提升类型会看起来一个扩展都不需要。

`ApiVersion` 默认 1.0（每个实现都有），所以答案永远不会偏乐观；调高它，已提升的对象就从
列表里掉出去。

**槽是独立的链，但槽里的东西照样要开扩展**，所以链的遍历会递归进 `reference_storage` 的
槽——只走 pNext 会漏掉挂在槽里的结构体。

**Vulkan 有两张扩展表，链决定不了往哪张放**。这不是能检查出来的错误，是常态：
`VkDeviceGroupDeviceCreateInfo` 挂在 `VkDeviceCreateInfo` 上，但 `VK_KHR_device_group_creation`
是个 **instance** 扩展；`VkPhysicalDeviceFeatures2` 也一样（`VK_KHR_get_physical_device_properties2`）。
所以不禁止混用，而是分开问：

```cpp
auto chain = param::device{} | param::option::device_group{} | param::feature::ext::mesh_shader{};
vkfu::required_instance_extensions<VK_API_VERSION_1_0>(chain);  // {"VK_KHR_device_group_creation"}
vkfu::required_device_extensions<VK_API_VERSION_1_0>(chain);    // {"VK_EXT_mesh_shader"}
```

## create 系列

生成器为 74 个 `vkCreate*` / `vkAllocate*` 生成包装，参数吃**表达式**，内部求值 + unpack。
每个两个重载，形态照 vkkl：抛 `VkResult` 的，和吃 `std::nothrow_t` 返回 `std::expected` 的。
命令只有三种骨架：

| 形态 | 数量 | 签名 |
| --- | --- | --- |
| 一个结构体、一个句柄 | 64 | `create_device(physical_device, expression, callbacks)` |
| 一次多个结构体 | 8 | `create_graphics_pipelines(device, cache, callbacks, e1, e2, ...)` → `std::array`，另附单数版 `create_graphics_pipeline` |
| 数量写在结构体里 | 2 | `allocate_command_buffers(device, expression, span<VkCommandBuffer> out)` |

厂商标记同样是命名空间：`vkfu::khr::create_swapchain`。约束是
`expression_for<Expression, obj::x>`，所以塞错结构体的表达式接不上。

## 用法

```bash
python -m vkfu_gen dump-ir  --xml vk.xml --out ir.json
python -m vkfu_gen rebuild  --xml vk.xml --table naming.toml --overrides naming.overrides.toml
python -m vkfu_gen suggest  --xml vk.xml --table naming.toml --out naming.suggested.toml
python -m vkfu_gen promote  --xml vk.xml --table naming.toml --out naming.suggested.toml
python -m vkfu_gen gen      --xml vk.xml --table naming.toml --scope closure \
    --out ../include/vkfu/generated/vulkan-v1.4.328.h
```

`suggest` 只列出表里还没有的条目；复核并就地改完后 `promote` 把它折进 `naming.toml`
（已有条目一律不动）。此后 `naming.toml` 就是手工维护的文件。

`--scope closure` 要求闭包里每个对象都有名字，缺一个就失败。`--scope table`（默认）
只生成命名过的，并报告还差多少，适合按用到才填表的增量节奏。

`gen` 在这些情况下**硬失败**：缺名字、名字撞车、**类作用域撞名**（字段、flags 的嵌套
`<字段>_type`、槽的 `<字段>_expression` 模板参数、`vulkan_tag_type` / `storage_type`
别名之间任意两者相撞）、枚举项撞名、遇到未支持的成员形态（多维数组、位无法一位一格
的 flag 枚举）。**只警告**：字段名与所属对象同名、字段名遮蔽它自己的枚举类型
（合法，因为生成的代码始终写全限定名）、表里命名了不在作用域内的标识符。

## 无 sType 的结构体也有 param

`VkPipelineColorBlendAttachmentState`、`VkDescriptorSetLayoutBinding`、`VkStencilOpState`、
`VkViewport` 这类 69 个结构体没有 pNext 要管，所以只生成 param 和 `evaluate()`——没有 tag、
没有 trait、没有链式钩子，也不能进管道。但 param 本身仍然值得有：

```cpp
param::state::color_blend_attachment{.color_write_mask = {.r = 1, .g = 1, .b = 1, .a = 1}}
param::descriptor_set_layout_binding{
    .descriptor_type = descriptor_type::uniform_buffer,
    .stage_flags = {.vertex = 1, .fragment = 1},
}
```

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

## 原生结构体与 CPO 钩子

原生 Vulkan 结构体长不出成员函数，所以三个 CPO 各留一个**前缀钩子**，作为 member / ADL
之后的第三档：`_vkfu_address`、`_vkfu_set_next`、`_vkfu_evaluate`。生成器为每个已知结构体
生成**独立的重载**（不是受约束的模板），因此影响面精确等于 vkfu 认识的那 507 个类型——
一个只是长得像（有 `sType`/`pNext`）但 vkfu 不认识的结构体不会被误认领：

```cpp
struct look_alike { VkStructureType sType; void const* pNext; };
static_assert(!vkfu::storable<look_alike>);   // tests/native_expression.cpp
```

配上 `expression_vulkan_tag` 的特化，原生结构体本身就是 expression，`evaluate` 对它是恒等，
可以和 param 混在同一条链里。

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
| 拆分 | `push` `constant` | `pushconstantPipelineLayout` → `pushconstant_...` |
| 词汇 | 26 个长单词（`rasterization` `maintenance` `premultiplied` …） | 让 `wordrun` 标记不被刷屏 |

另有一条规则不靠词典：**缩写后紧跟的小写 `s` 是复数标记**，所以 `pStdSPSs` → `std_sps`
而不是 `std_sp_ss`。

复核标记（只出现在 `naming.suggested.toml` 里）：

| tag | 含义 |
| --- | --- |
| `restates` | 字段名复述了对象名，但它不是开关，`enable` 不适用 |
| `repeat` | 名字里有重复的词，通常说明命名空间或父节点已经带了那段前缀（`device_device_memory_report`） |
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

## flags 与枚举的两个已知边界

- **clang 目前没有位域上的 constexpr `bit_cast`**，所以逐位的 `static_assert` 用
  `#if !defined(__clang__)` 跳过；GCC/MSVC 上 507 个对象的位布局全部编译期验证，
  clang 侧由运行期测试（`typical_structures` 的 `test_flag_bits`）覆盖。
- **枚举的 span/数组保持 native 元素类型**。`state::dynamic::states` 是
  `span<VkDynamicState const>` 而不是 `span<enums::dynamic_state const>`：span 是借用，
  换元素类型就得复制，而按别的枚举类型读同一块内存违反类型别名规则。标量枚举字段
  则一律包成 `enum class`。

## 编译验证

WSL Arch 里 g++ 16.1.1 与 clang++ 22.1.8，配 v1.4.328 的 Vulkan-Headers（与 vk.xml 同版本），
C++23、`-Wall -Wextra`、**零警告**：

- 可运行：`typical_structures` `native_expression` `pipeline_slots` `triangle_pipeline`
  `chain` `frame_recording`
- 只编译（会调 loader）：`producers.cpp` `commands.cpp`、`examples/bootstrap.cpp`

编译器抓到过 6 个我靠标识符回查发现不了的真错误：嵌套类的 DMI 在外层类闭合前不可用、
ADL 只关联最内层命名空间（`operator|` 整个用不了）、GCC 对非模板 `requires` 不做 SFINAE、
clang 没有位域上的 constexpr `bit_cast`、位名跨厂商撞名导致位域重复声明、
命令级守卫宏漏收。

## demo 是怎么验证的

`demo/consumer-arch` 要 SDL3 / glslang / bvn 平台层才能链接，在这里编不了。所以：

- 它现在用的**每一个表达式**都镜像进了 `tests/frame_recording.cpp`——实例、设备、交换链、
  image view、命令缓冲开始/继承、屏障、依赖、动态渲染、提交、呈现——断言出来的原生结构体
  与原先手写的赋值逐字段一致，并且拷贝之后槽仍指向自己那份子存储。
- 另外对 demo 源码做静态回查：每个 `param::X` 存在、每个顶层指定初始化器是该结构体的真字段、
  19 个位域名、每个 `enums::X::Y`、每个 `vkfu::create_*` / `allocate_*` 都对得上，且没有 vkb 残留。

## 已知欠账

- 创建期元信息（physical_device / allocator / 池句柄）与 `create_*` 函数还没做；
  框架侧也还没有"非 pNext 节点"这个概念。
- 查询侧未做（`returnedonly` + `vkGet*` 的出参）。
- **没有 `sType` 的值结构体不生成 param**：`VkPipelineColorBlendAttachmentState`、`VkViewport`、
  `VkVertexInputBindingDescription` 这些没有 pNext 要管，直接用原生聚合初始化就行。这是
  刻意的边界，不是遗漏。
- 枚举的 span/数组保持 native 元素类型（见上）。
- 引擎侧 `include/bvn/graphics/vulkan_renderer.h` 和 `source/client/context.h` 还在用
  vk-bootstrap，所以 `vcpkg.json` 里的依赖没有摘掉。demo 已经不用它了。
- 查询链只能读 native 字段名，没有读侧的 snake 化 param。这是取舍：246 个结构体的 1027 个
  字段进命名表，换来的收益不明显。
- 重复检查是**逐层**的，这是完整的：表达式槽持有的是一条独立的 pNext 链，不是父链的延续，
  所以"跨层重复"按规范本来就合法。全树扁平化检查的代价（`|` 折叠时每步重查前缀）不值得放进
  默认路径；真要验证就在需要的地方写一次。
- 表达式槽和 `reference.h` 还在等 review。
- 本机没有编译器和 Vulkan 头，生成结果尚未编译验证（已做标识符回查与结构自检）。
- 一个 count 被多个指针共用时（如 `VkWriteDescriptorSet`）不折叠成 span，
  保留 count + 裸指针。
- **指针带 `optional` 时也不折叠**（17 处）：`viewportCount == 1` 配 `pViewports == NULL`
  是动态视口的合法写法，span 表达不出来，所以 count 与指针都保持原样。代价是
  `graphics_pipeline` 的 `.stage_count` / `.stages`、`color_blend` 的
  `.attachment_count` / `.attachments` 要自己对齐——和手写 Vulkan 一样。
- `option::` 里还有几个很长的名字（最长 `option::ray_tracing_pipeline_cluster_acceleration_structure`）。
  它们的前缀多是"挂在谁身上"，调用点其实已经能看出来，进一步按父节点分层可以再短一截——
  但 30 个对象有多个父节点，需要逐个定。
