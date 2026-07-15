#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <stdexec/execution.hpp>
#include <nagisa/concurrency/coroutine.h>

#include <bvn/graphics/render_workflow.h>
#include <bvn/graphics/renderable.h>
#include <bvn/graphics/vulkan_renderer.h>
#include <bvn/gameplay/entity.h>
#include <bvn/platform/window.h>

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
		context_owner = &game_context;
		render_workflow = &game_context.render_workflow;
		if (auto existing = game_context.registry.ctx().find<preview_state>(); existing != nullptr)
		{
			preview = existing;
		}
		else
		{
			preview = &game_context.registry.ctx().emplace<preview_state>();
		}
		game_context.render_scope.spawn(::stdexec::starts_on(game_context.render_workflow.get_scheduler(), ::bvn::graphics::render(*this, ::bvn::graphics::dynamic_forward_global_env_renderer(game_context.renderer.global_env()))));
		co_return;
	}

	auto render(::bvn::graphics::global_dynamic_forward_env_renderer global) -> ::bvn::gameplay::task
	{
		auto env = co_await ::nagisa::concurrency::environment();
		auto stop = ::stdexec::get_stop_token(env);
		auto scheduler = ::stdexec::get_scheduler(env);

		auto imgui = imgui_lifetime{};
		imgui.device = global.device();

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

		auto color_format = global.swapchain_image_format();
		auto pipeline_rendering = VkPipelineRenderingCreateInfo{};
		pipeline_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		pipeline_rendering.colorAttachmentCount = 1;
		pipeline_rendering.pColorAttachmentFormats = &color_format;

		auto image_count = ::std::max(global.swapchain_image_count(), 2u);
		auto init_info = ImGui_ImplVulkan_InitInfo{};
		init_info.ApiVersion = VK_API_VERSION_1_3;
		init_info.Instance = global.instance();
		init_info.PhysicalDevice = global.physical_device();
		init_info.Device = global.device();
		init_info.QueueFamily = global.graphics_queue_family();
		init_info.Queue = global.graphics_queue();
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

		auto secondary_commands = ::bvn::graphics::secondary_command_pool{global.device(), global.graphics_queue_family()};
		auto retirements = ::stdexec::simple_counting_scope{};
		auto render_error = ::std::exception_ptr{};

		try
		{
			while (!stop.stop_requested())
			{
				auto frame = co_await render_workflow->async_record(secondary_commands);

				if (!frame || stop.stop_requested())
				{
					break;
				}

				auto command_buffer = frame.allocate();
				auto command = command_buffer.get();

				auto draw_data = static_cast<::ImDrawData*>(nullptr);
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
						auto extent = global.swapchain_extent();
						::ImGui::Text("swapchain: %u x %u", extent.width, extent.height);
						::ImGui::Text("device: %s", global.device_name());
					}

					::ImGui::End();
					::ImGui::Render();
					draw_data = ::ImGui::GetDrawData();
				}

				auto inheritance_color_format = global.swapchain_image_format();
				auto rendering_info = VkCommandBufferInheritanceRenderingInfo{};
				rendering_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
				rendering_info.colorAttachmentCount = 1;
				rendering_info.pColorAttachmentFormats = &inheritance_color_format;
				rendering_info.depthAttachmentFormat = global.depth_format();
				rendering_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

				auto inheritance_info = VkCommandBufferInheritanceInfo{};
				inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
				inheritance_info.pNext = &rendering_info;

				auto begin_info = VkCommandBufferBeginInfo{};
				begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
				begin_info.pInheritanceInfo = &inheritance_info;

				if (::vkBeginCommandBuffer(command, &begin_info) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to begin debug overlay secondary command buffer"};
				}

				if (draw_data != nullptr)
				{
					::ImGui_ImplVulkan_RenderDrawData(draw_data, command);
				}

				if (::vkEndCommandBuffer(command) != ::VK_SUCCESS)
				{
					throw ::std::runtime_error{"failed to end debug overlay secondary command buffer"};
				}

				::stdexec::spawn(frame.retire(::std::move(command_buffer)), retirements.get_token());
			}
		}
		catch (...)
		{
			render_error = ::std::current_exception();
		}

		retirements.close();
		co_await ::stdexec::unstoppable(::stdexec::starts_on(scheduler, retirements.join()));
		if (render_error)
		{
			::std::rethrow_exception(render_error);
		}
	}

	context* context_owner = nullptr;
	::bvn::platform::window* window = nullptr;
	::bvn::graphics::render_workflow* render_workflow = nullptr;
	preview_state* preview = nullptr;
};

static_assert(::bvn::graphics::renderable<debug_overlay&, ::bvn::graphics::global_dynamic_forward_env_renderer>);
}
