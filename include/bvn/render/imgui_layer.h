#pragma once

#include <cstdint>
#include <chrono>

#include <SDL3/SDL_events.h>

#include <bvn/platform/window.h>
#include <bvn/rhi/vulkan_context.h>

namespace bvn::render
{
	struct debug_overlay_state
	{
		::std::chrono::milliseconds frame_time{};
		double sim_alpha = 0.0;
		std::uint64_t tick = 0;
	};

	struct imgui_layer
	{
		imgui_layer(platform::window const& window, rhi::vulkan_context& context);
		~imgui_layer() noexcept;

		imgui_layer(imgui_layer const&) = delete;
		auto operator=(imgui_layer const&) -> imgui_layer& = delete;

		imgui_layer(imgui_layer&&) = delete;
		auto operator=(imgui_layer&&) -> imgui_layer& = delete;

		void process_event(SDL_Event const& event) noexcept;
		void after_swapchain_recreated(rhi::vulkan_context const& context) noexcept;
		void new_frame();
		void build_debug_overlay(debug_overlay_state const& state);
		void render();
		void record(VkCommandBuffer command_buffer) const;

	private:
		rhi::vulkan_context* context = nullptr;
	};
}
