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

	using guard_type = ::nagisa::concurrency::binary_semaphore_guard;

	// menu 的访问权：update 的完成值，只移动。持有期间 menu 的变量（当前状态、command pool）归持有者独占；
	// record 录制的命令缓冲析构时要释放回 pool，所以 access 必须比 record 交出的命令缓冲活得久。
	struct access
	{
		guard_type::permit _permit;
		state _snapshot;

		[[nodiscard]] state const& snapshot() const noexcept { return _snapshot; }
	};

	// graphics_queue 只在构造时用于上传字体纹理，调用方需保证此时没有别人使用它。
	explicit menu(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format, ::std::filesystem::path const& font_path = {});
	~menu();
	menu(menu&&) noexcept;
	menu& operator=(menu&&) noexcept;

	// 起始点：取得访问权后按旧状态与 keys 计算新状态并写回，完成值携带 permit 与新状态的快照。
	[[nodiscard]] values_sender_type<access> update(input::state_type const& keys);
	[[nodiscard]] values_sender_type<::std::vector<::vkkl::command_buffer>, ::std::vector<buffer_type>> record(access const& access, ::VkExtent2D extent) const;

	struct impl;
	::std::unique_ptr<impl> _impl;
};
