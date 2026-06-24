#include <algorithm>
#include <stdexcept>
#include <string>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <bvn/render/imgui_layer.h>

namespace bvn::render
{
	namespace
	{
		void check(VkResult result, char const* message)
		{
			if (result != VK_SUCCESS)
			{
				throw std::runtime_error(std::string{message} + " VkResult=" + std::to_string(static_cast<int>(result)));
			}
		}

	}

	imgui_layer::imgui_layer(platform::window const& window, rhi::vulkan_context& vulkan_context)
		: context(&vulkan_context)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		auto& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		if (!ImGui_ImplSDL3_InitForVulkan(window.native()))
		{
			throw std::runtime_error{"failed to initialize ImGui SDL3 backend"};
		}

		auto color_format = vulkan_context.swapchain_image_format;
		auto pipeline_rendering = VkPipelineRenderingCreateInfo{};
		pipeline_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		pipeline_rendering.colorAttachmentCount = 1;
		pipeline_rendering.pColorAttachmentFormats = &color_format;

		auto image_count = std::max(static_cast<std::uint32_t>(vulkan_context.swapchain_images.size()), 2u);
		auto init_info = ImGui_ImplVulkan_InitInfo{};
		init_info.ApiVersion = VK_API_VERSION_1_3;
		init_info.Instance = vulkan_context.instance;
		init_info.PhysicalDevice = vulkan_context.physical_device;
		init_info.Device = vulkan_context.device;
		init_info.QueueFamily = vulkan_context.graphics_queue_family;
		init_info.Queue = vulkan_context.graphics_queue;
		init_info.DescriptorPoolSize = 64;
		init_info.MinImageCount = 2;
		init_info.ImageCount = image_count;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.UseDynamicRendering = true;
		init_info.PipelineRenderingCreateInfo = pipeline_rendering;
		init_info.CheckVkResultFn = [](VkResult result)
		{
			check(result, "ImGui Vulkan backend failure");
		};

		if (!ImGui_ImplVulkan_Init(&init_info))
		{
			throw std::runtime_error{"failed to initialize ImGui Vulkan backend"};
		}

		if (!ImGui_ImplVulkan_CreateFontsTexture())
		{
			throw std::runtime_error{"failed to create ImGui font texture"};
		}
	}

	imgui_layer::~imgui_layer() noexcept
	{
		if (context != nullptr && context->device != VK_NULL_HANDLE)
		{
			(void)vkDeviceWaitIdle(context->device);
		}

		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
	}

	void imgui_layer::process_event(SDL_Event const& event) noexcept
	{
		ImGui_ImplSDL3_ProcessEvent(&event);
	}

	void imgui_layer::after_swapchain_recreated(rhi::vulkan_context const& context) noexcept
	{
		ImGui_ImplVulkan_SetMinImageCount(std::max(static_cast<std::uint32_t>(context.swapchain_images.size()), 2u));
	}

	void imgui_layer::new_frame()
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
	}

	void imgui_layer::build_debug_overlay(debug_overlay_state const& state)
	{
		ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize({280.0f, 132.0f}, ImGuiCond_FirstUseEver);

		if (ImGui::Begin("bvn m1"))
		{
			ImGui::Text("tick: %llu", static_cast<unsigned long long>(state.tick));
			ImGui::Text("frame: %lld ms", static_cast<long long>(state.frame_time.count()));
			ImGui::Text("sim alpha: %.3f", state.sim_alpha);

			if (context != nullptr)
			{
				auto extent = context->swapchain_extent;
				ImGui::Text("swapchain: %u x %u", extent.width, extent.height);
				ImGui::Text("device: %s", context->device_name);
			}
		}

		ImGui::End();
	}

	void imgui_layer::render()
	{
		ImGui::Render();
	}

	void imgui_layer::record(VkCommandBuffer command_buffer) const
	{
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
	}
}
