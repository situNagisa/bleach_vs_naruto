// demo.scene.menu 的声明，唯一来源。三种展开形态见 execution/execution.h 开头与 README §3。
#if defined(BVN_EXEC0_MODULES) && !defined(BVN_EXEC0_MODULE_INTERFACE)

import std;
import demo.execution;
import demo.renderable.buffer_type;
import demo.input;
import demo.render;
import demo.scene.menu;

#else

#if !defined(BVN_EXEC0_MODULE_INTERFACE)
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>
#include <vkkl/command_buffer.h>

#include "../../execution/execution.h"
#include "../../renderable/buffer_type.h"
#include "../../input/input.h"
#include "../../render/renderer.h"
#endif

#ifndef BVN_EXEC0_SCENE_MENU_MENU_H
#define BVN_EXEC0_SCENE_MENU_MENU_H

struct menu
{
	enum class action { none, start_game, quit };

	struct state
	{
		::std::size_t selected = 0;
		action requested = action::none;
	};

	explicit menu(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format, ::std::filesystem::path const& font_path = {});
	~menu();
	menu(menu&&) noexcept;
	menu& operator=(menu&&) noexcept;

	[[nodiscard]] values_sender_type<state> update(input::state_type const& keys) const;
	[[nodiscard]] values_sender_type<::std::vector<::vkkl::command_buffer>, ::std::vector<buffer_type>> record(state const& snapshot, ::VkExtent2D extent) const;

	state current_state{};

	struct impl;
	::std::unique_ptr<impl> _impl;
};

#endif

#endif
