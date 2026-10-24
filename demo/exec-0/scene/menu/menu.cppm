module;

#include <vulkan/vulkan.h>
#include <vkkl/command_buffer.h>

export module demo.scene.menu;

import std;
import demo.execution;
export import demo.renderable.buffer_type;
import demo.input;
import demo.render;

export struct menu
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
