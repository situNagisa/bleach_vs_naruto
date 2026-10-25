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

	// 持久状态：当前选中项。只有持有 guard 的 permit 时才能读写。
	struct state_type
	{
		::std::size_t selected = 0;
	};

	// 变化值：本次 update 选中项是否变化，以及本次请求的动作（事件，不进入 state）。
	struct change_type
	{
		bool selection_changed = false;
		action requested = action::none;
	};

	using guard_type = ::nagisa::concurrency::binary_semaphore_guard;

	// graphics_queue 只在构造时用于上传字体纹理，调用方需保证此时没有别人使用它。
	explicit menu(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format, ::std::filesystem::path const& font_path = {});
	~menu();
	menu(menu&&) noexcept;
	menu& operator=(menu&&) noexcept;

	// 开放访问：state 与 command pool 由 guard 保护，只有持有它的 permit 时才能访问。
	// 二者都放在 impl 里（menu 要可移动，guard 不可移动），经这两个成员函数取用。
	[[nodiscard]] state_type& state() const noexcept;
	[[nodiscard]] guard_type& guard() const noexcept;

	// 起始点：取得 permit 后按 state 与 input 的变化值计算新状态并写入 state，完成值是 (permit, 变化值)。
	// menu 不析构 permit：调用方在本帧对 state / command pool 的访问全部结束后自行销毁。
	[[nodiscard]] values_sender_type<guard_type::permit, change_type> update(input::change_type const& keys);
	// 读 state、从 command pool 分配命令缓冲，所以要出示本 menu 的 permit。
	// permit 必须比交出的命令缓冲活得久：命令缓冲析构时释放回 command pool。
	[[nodiscard]] values_sender_type<::std::vector<::vkkl::command_buffer>, ::std::vector<buffer_type>> record(guard_type::permit const& permit, ::VkExtent2D extent) const;

	struct impl;
	::std::unique_ptr<impl> _impl;
};
