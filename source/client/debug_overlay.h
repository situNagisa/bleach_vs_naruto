#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <stdexcept>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <stdexec/execution.hpp>
#include <nagisa/concurrency/coroutine.h>

#include <bvn/display_architecture/renderable.h>
#include <bvn/gameplay/entity.h>
#include <bvn/platform/window.h>
#include <bvn/renderer/vulkan_renderer.h>

#include "context.h"
#include "preview_state.h"

namespace
{
struct debug_overlay
{
	struct imgui_lifetime
	{
		::VkDevice device = VK_NULL_HANDLE;
		bool context_initialized = false;
		bool sdl_backend_initialized = false;
		bool vulkan_backend_initialized = false;

		imgui_lifetime() = default;

		imgui_lifetime(imgui_lifetime const&) = delete;
		auto operator=(imgui_lifetime const&) -> imgui_lifetime& = delete;

		~imgui_lifetime() noexcept
		{
			if (device != VK_NULL_HANDLE && (vulkan_backend_initialized || sdl_backend_initialized || context_initialized))
			{
				(void)::vkDeviceWaitIdle(device);
			}

			if (vulkan_backend_initialized)
			{
				::ImGui_ImplVulkan_Shutdown();
			}

			if (sdl_backend_initialized)
			{
				::ImGui_ImplSDL3_Shutdown();
			}

			if (context_initialized)
			{
				::ImGui::DestroyContext();
			}
		}
	};

	auto main(context& game_context) -> ::bvn::gameplay::task
	{
		window = &game_context.window;
		owner = &game_context.renderer;
		context_owner = &game_context;
		render_workflow_scheduler = ::stdexec::get_scheduler(game_context.render_workflow);
		if (auto existing = game_context.registry.ctx().find<preview_state>(); existing != nullptr)
		{
			preview = existing;
		}
		else
		{
			preview = &game_context.registry.ctx().emplace<preview_state>();
		}
		game_context.render_scope.spawn(::stdexec::starts_on(::stdexec::get_scheduler(game_context.render_workflow), ::bvn::display_architecture::render(*this, game_context.renderer)));
		co_return;
	}

	auto render(::bvn::renderer::vulkan_renderer& renderer) -> ::bvn::gameplay::task
	{
		auto env = co_await ::nagisa::concurrency::environment();
		auto stop = ::stdexec::get_stop_token(env);

		auto imgui = imgui_lifetime{};
		imgui.device = renderer.device.handle;

		IMGUI_CHECKVERSION();
		::ImGui::CreateContext();
		imgui.context_initialized = true;
		::ImGui::StyleColorsDark();

		auto&& io = ::ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		if (!::ImGui_ImplSDL3_InitForVulkan(window->handle))
		{
			throw ::std::runtime_error{"failed to initialize ImGui SDL3 backend"};
		}
		imgui.sdl_backend_initialized = true;

		auto color_format = renderer.swapchain_image_format;
		auto pipeline_rendering = VkPipelineRenderingCreateInfo{};
		pipeline_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		pipeline_rendering.colorAttachmentCount = 1;
		pipeline_rendering.pColorAttachmentFormats = &color_format;

		auto image_count = ::std::max(static_cast<::std::uint32_t>(renderer.swapchain_images.size()), 2u);
		auto init_info = ImGui_ImplVulkan_InitInfo{};
		init_info.ApiVersion = VK_API_VERSION_1_3;
		init_info.Instance = renderer.instance.handle;
		init_info.PhysicalDevice = renderer.physical_device;
		init_info.Device = renderer.device.handle;
		init_info.QueueFamily = renderer.graphics_queue_family;
		init_info.Queue = renderer.graphics_queue;
		init_info.DescriptorPoolSize = 64;
		init_info.MinImageCount = 2;
		init_info.ImageCount = image_count;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.UseDynamicRendering = true;
		init_info.PipelineRenderingCreateInfo = pipeline_rendering;
		init_info.CheckVkResultFn = [](VkResult result)
		{
			if (result != ::VK_SUCCESS)
			{
				throw ::std::runtime_error{"ImGui Vulkan backend failure"};
			}
		};

		if (!::ImGui_ImplVulkan_Init(&init_info))
		{
			throw ::std::runtime_error{"failed to initialize ImGui Vulkan backend"};
		}
		imgui.vulkan_backend_initialized = true;

		if (!::ImGui_ImplVulkan_CreateFontsTexture())
		{
			throw ::std::runtime_error{"failed to create ImGui font texture"};
		}

		while (!stop.stop_requested())
		{
			co_await ::stdexec::schedule(render_workflow_scheduler);

			if (stop.stop_requested())
			{
				break;
			}

			if (context_owner != nullptr)
			{
				auto events = ::bvn::platform::event_state{};
				{
					auto lock = ::std::lock_guard{context_owner->events_mutex};
					events = context_owner->events;
				}

				for (auto&& event : events.sdl_events)
				{
					::ImGui_ImplSDL3_ProcessEvent(&event);
				}

				::ImGui_ImplVulkan_NewFrame();
				::ImGui_ImplSDL3_NewFrame();
				::ImGui::NewFrame();
				preview->keyboard_captured.store(::ImGui::GetIO().WantCaptureKeyboard, ::std::memory_order_release);

				auto control_is_camera = false;
				auto tick = ::std::uint64_t{};
				auto frame_time_ms = 0ll;
				auto sim_alpha = 0.0;
				auto hero_speed = 0.0f;

				{
					auto lock = ::std::lock_guard{preview->data_mutex};
					control_is_camera = preview->control_is_camera;
					tick = preview->tick;
					frame_time_ms = preview->frame_time_ms;
					sim_alpha = preview->sim_alpha;
					hero_speed = preview->hero_speed;
				}

				::ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
				::ImGui::SetNextWindowSize({320.0f, 200.0f}, ImGuiCond_FirstUseEver);

				if (::ImGui::Begin("bvn m1"))
				{
					::ImGui::Text("control: %s", control_is_camera ? "camera (Minecraft)" : "hero (BvN)");

					if (::ImGui::Button(control_is_camera ? "switch to hero  [F1]" : "switch to camera  [F1]"))
					{
						preview->control_toggle_requested.store(true, ::std::memory_order_release);
					}

					::ImGui::TextUnformatted("camera: WASD move, Space/LShift up-down, mouse look");
					::ImGui::TextUnformatted("hero: WASD move, LShift to run");
					::ImGui::Separator();
					::ImGui::Text("tick: %llu", static_cast<unsigned long long>(tick));
					::ImGui::Text("frame: %lld ms", frame_time_ms);
					::ImGui::Text("sim alpha: %.3f", sim_alpha);
					::ImGui::Text("hero speed: %.2f", static_cast<double>(hero_speed));
					::ImGui::Text("swapchain: %u x %u", owner->swapchain_extent.width, owner->swapchain_extent.height);
					::ImGui::Text("device: %s", owner->device_name);
				}

				::ImGui::End();
				::ImGui::Render();

				if (auto draw_data = ::ImGui::GetDrawData(); draw_data != nullptr)
				{
					::ImGui_ImplVulkan_RenderDrawData(draw_data, renderer.command_buffer);
				}
			}
		}
	}

	context* context_owner = nullptr;
	::bvn::platform::window* window = nullptr;
	::bvn::renderer::vulkan_renderer* owner = nullptr;
	render_workflow::scheduler render_workflow_scheduler{};
	preview_state* preview = nullptr;
};

static_assert(::bvn::display_architecture::renderable<debug_overlay&, ::bvn::renderer::vulkan_renderer&>);
}
