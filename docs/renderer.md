# bvn 渲染器（renderer）· 设计方向

> 日期：2026-06-25　状态：**方向已定**
> 本文记录「渲染器」的设计思想，**用于描述项目、非实现计划**。参考 `engine-spec.md §4.5`（英雄产出渲染任务）的思路。

---

## 1. 核心哲学：初期使用vulkan，后期慢慢萃取出规范

`renderer`本身足够复杂（参考`vulkan`，`opengl`的设计都很大一坨），**所以妄图凭空设计一个足够优秀的渲染器是不可能的**。
为了保证开发，我们初期的`renderer`用`vulkan`，即将`vulkan`当做我们的`renderer`规范，我们写的不是vulkan，而是`renderer`规范，以此来限定`vulkan`的使用范围。

------

## 2. `vulkan_renderer` 的形态与职责划分

### 2.1 初始化

游戏主体在游戏开始的合适时机初始化`vulkan_renderer`，英雄可以通过`context`读取到它，从而使用`vulkan_context`创建自己的Pipeline / Buffer / Texture 等 GPU 资源。

``` cpp
struct hero
{
	render_task render(vulkan_renderer& renderer)
	{
		// 初始化自己需要的资源
		auto vert_module = renderer.create_shader_module(/* 读取 hero.vert.spv */);
		auto frag_module = renderer.create_shader_module(/* 读取 hero.frag.spv */);
		VkPipelineShaderStageCreateInfo stages[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" },
        };
        // 英雄自己决定 vertex 格式（比如 2D sprite 的 pos+uv）
        VkPipelineVertexInputStateCreateInfo vertex_input{ /* 英雄定义的 layout */ };
        // 英雄自己决定混合模式（比如要不要透明度混合，这正是 2D sprite 渲染的核心需求）
        VkPipelineColorBlendAttachmentState blend{ /* 英雄定义的 blend state */ };
        auto pipeline_layout = renderer.create_pipeline_layout(/* 自己的 descriptor set layout */);
        auto pipeline = renderer.create_graphics_pipeline(stages, 2, vertex_input, blend, pipeline_layout);

        auto vb = renderer.create_buffer(/* size */, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, /* ... */);
        vertex_buffer = vb.buffer;
        co_return;
	}
	hero_task kenpachi(hero_context context)
    {
        // 启动绘制
        spawn(context.render_scheduler(), render(*this, context.vulkan_renderer));
        co_return;
    }
}
```

### 2.2 绘制期

```cpp
render_task render(vulkan_renderer& renderer)
{
    // 初始化自己需要的资源
    // ...
    while(alive())
    {
        vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(ctx.command_buffer, 0, 1, &vertex_buffer, offsets);

        // 英雄完全自由：可以 push constant 传位置、可以 bind 自己的贴图 descriptor set
        // vkCmdPushConstants(...);
        // vkCmdBindDescriptorSets(...);

        vkCmdDraw(ctx.command_buffer, /*vertex_count*/ 6, 1, 0, 0); // 画自己的四边形/精灵
        co_await next_render();
    }
    co_return;
}
```

### 2.3 `vulkan_renderer` 持有什么

renderer 作为持久对象，负责管理所有框架级的 Vulkan 状态：

```cpp
struct vulkan_renderer
{
    // 核心对象（一次性建立）
    VkInstance       instance;
    VkSurfaceKHR     surface;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          graphics_queue, present_queue;

    // SwapChain
    VkSwapchainKHR           swapchain;
    VkFormat                 swapchain_format;
    VkExtent2D               swapchain_extent;
    std::vector<VkImage>     swapchain_images;
    std::vector<VkImageView> swapchain_image_views;

    // RenderPass / Framebuffer（renderer 决定其结构，renderable 在此框架内自由发挥）
    VkRenderPass               render_pass;
    std::vector<VkFramebuffer> framebuffers;

    // 命令录制（每个 in-flight frame 一个 CommandBuffer）
    VkCommandPool                command_pool;
    std::vector<VkCommandBuffer> command_buffers;

    // 同步对象
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence>     in_flight_fences;

    static constexpr uint32_t max_frames_in_flight = 2;
    uint32_t current_frame = 0;
};
```

### 2.4`vulkan_renderer` 给 renderable 的辅助接口（初始化期用）（用户可选）

renderer 把 Vulkan 的繁文缛节收起来，但**所有"画法"语义参数（vertex layout、blend mode、shader）由 renderable 自己决定**：

```cpp
// Shader 编译
VkShaderModule create_shader_module(const std::vector<char>& spirv_code) const;

// Pipeline 创建：renderer 处理样板，renderable 填画法参数
VkPipeline create_graphics_pipeline(
    const VkPipelineShaderStageCreateInfo* stages, uint32_t stage_count,
    const VkPipelineVertexInputStateCreateInfo& vertex_input,
    const VkPipelineColorBlendAttachmentState&  blend_state,
    VkPipelineLayout layout) const;

VkPipelineLayout create_pipeline_layout(
    const VkDescriptorSetLayout* set_layouts, uint32_t set_layout_count) const;

// Buffer 创建（内部处理内存分配，实现可换 VMA）
struct buffer_handle { VkBuffer buffer; VkDeviceMemory memory; };
buffer_handle create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties) const;
```

### 2.5 RenderPass 边界（现阶段约定）

renderer 决定 RenderPass 的结构（初期：单 color attachment，不搞多 subpass/MSAA）；renderable 在这个框架内自由决定 Pipeline 的其余所有状态。需要多 pass 效果（描边、后处理）时再扩展 `renderer ` 暴露 subpass 切换能力。