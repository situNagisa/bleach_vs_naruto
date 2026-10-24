module;

#include <cassert>
#include <cstdlib>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <glm/glm.hpp>
#include <vkfu/vulkan/v1.4.328/version_1_0.h>
#include <vkfu/vulkan/v1.4.328/version_1_3.h>
#include <vkkl/command_pool.h>
#include <vkkl/command_buffer.h>

module demo.scene.menu;

import demo.renderable.vector_graph;
import demo.renderable.font;

namespace
{
::std::filesystem::path environment_path(char const* name)
{
#ifdef _WIN32
	auto const wide_name = ::std::filesystem::path{ name }.wstring();
	auto const count = ::GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
	if (!count)
		return {};
	auto value = ::std::wstring(count, L'\0');
	auto const written = ::GetEnvironmentVariableW(wide_name.c_str(), value.data(), count);
	if (!written || written >= count)
		throw ::std::runtime_error{ "menu: environment changed while reading font path" };
	value.resize(written);
	return ::std::filesystem::path{ value };
#else
	if (auto const value = ::std::getenv(name))
		return ::std::filesystem::path{ value };
	return {};
#endif
}

::std::filesystem::path find_font(::std::filesystem::path const& requested)
{
	if (!requested.empty())
		return requested;
	if (auto const path = ::environment_path("BVN_FONT"); !path.empty())
		return path;
	auto candidates = ::std::vector<::std::filesystem::path>{};
#ifdef _WIN32
	if (auto const windows = ::environment_path("WINDIR"); !windows.empty())
		candidates.emplace_back(windows / "Fonts" / "segoeui.ttf");
#endif
	candidates.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
	candidates.emplace_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
	candidates.emplace_back("/System/Library/Fonts/Supplemental/Arial.ttf");
	for (auto const& candidate : candidates)
		if (::std::filesystem::is_regular_file(candidate))
			return candidate;
	throw ::std::runtime_error{ "menu: no system font found; pass --font <TTF/OTF file> or set BVN_FONT" };
}

::glm::mat3 make_transform(::VkExtent2D viewport, float x, float y, float width, float height, ::VkExtent2D source)
{
	auto result = ::glm::mat3{ 1.0f };
	result[0][0] = 2.0f * width / (static_cast<float>(viewport.width) * static_cast<float>(source.width));
	result[1][1] = 2.0f * height / (static_cast<float>(viewport.height) * static_cast<float>(source.height));
	result[2][0] = 2.0f * x / static_cast<float>(viewport.width) - 1.0f;
	result[2][1] = 2.0f * y / static_cast<float>(viewport.height) - 1.0f;
	return result;
}
}

struct menu::impl
{
	::VkDevice device;
	::VkPhysicalDevice physical_device;
	::VkFormat format;
	vector_graph graph;
	::std::vector<font::text> labels;
	// 以上为常量（draw 只读管线与纹理）；以下为变量，只在持有 guard 的 permit 时访问。
	// pool：Vulkan 要求同一 command pool 的分配、录制、释放外部同步。
	::vkkl::command_pool pool;
	menu::state_type state{};
	guard_type guard{ 1 };

	explicit impl(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format, ::std::filesystem::path const& font_path)
		: device(device)
		, physical_device(physical_device)
		, format(format)
		, graph(device, format)
		, pool(device, ::vkfu::create_command_pool(device, ::vkfu::param::command_pool{ .flags = {.transient = 1}, .queue_family_index = graphics_queue_family }))
	{
		auto typeface = font{ ::find_font(font_path) };
		constexpr auto captions = ::std::array{ "BVN", "BLEACH  /  NARUTO", "Start Game", "Exit", "UP / DOWN   Select       ENTER / SPACE   Confirm", "ESC / K   Exit" };
		labels.reserve(captions.size());
		for (auto const [index, caption] : captions | ::std::views::enumerate)
			labels.push_back(typeface.prepare(caption, index == 0 ? 160u : 48u, device, physical_device,
				graphics_queue, graphics_queue_family, format));
	}
};

menu::menu(::VkDevice device, ::VkPhysicalDevice physical_device, ::VkQueue graphics_queue, ::std::uint32_t graphics_queue_family, ::VkFormat format, ::std::filesystem::path const& font_path)
	: _impl(::std::make_unique<impl>(device, physical_device, graphics_queue, graphics_queue_family, format, font_path)) {}
menu::~menu() = default;
menu::menu(menu&&) noexcept = default;
menu& menu::operator=(menu&&) noexcept= default;
menu::state_type& menu::state() const noexcept { return _impl->state; }
menu::guard_type& menu::guard() const noexcept { return _impl->guard; }

values_sender_type<menu::guard_type::permit, menu::change_type> menu::update(input::change_type const& keys)
{
	return _impl->guard.acquire() | ::stdexec::let_value([&state = _impl->state, keys](guard_type::permit& permit) {
		auto const pressed = [&keys](key_code key) { return keys[key] == key_change::pressed; };
		auto change = change_type{};
		if (keys.close_requested || pressed(key_code::quit) || pressed(key_code::b))
			change.requested = action::quit;
		else
		{
			if (pressed(key_code::up) != pressed(key_code::down))
			{
				state.selected = (state.selected + 1) % 2;
				change.selection_changed = true;
			}
			if (pressed(key_code::confirm) || pressed(key_code::a))
				change.requested = state.selected == 0 ? action::start_game : action::quit;
		}
		// state 已写入；permit 连同变化值交给调用方，由调用方决定何时释放。
		return ::stdexec::just(::std::move(permit), change);
	});
}

values_sender_type<::std::vector<::vkkl::command_buffer>, ::std::vector<buffer_type>> menu::record([[maybe_unused]] guard_type::permit const& permit, ::VkExtent2D extent) const
{
	// permit 必须来自本 entity 的 guard（比较 permit 记录的信号量地址）。
	assert(permit._semaphore == ::std::addressof(_impl->guard._semaphore));
	// 调用方持有 permit 直到交出的命令缓冲析构，所以 sender 运行时直接读 state 是安全的。
	return ::stdexec::just() | ::stdexec::let_value([this, extent] {
	assert(extent.width > 0 && extent.height > 0);
	auto commands = ::std::vector<::vkkl::command_buffer>{};
	auto& owner = commands.emplace_back(_impl->device, _impl->pool.handle, VK_NULL_HANDLE);
	::vkfu::allocate_command_buffers(_impl->device, ::vkfu::param::command_buffer{ .command_pool = _impl->pool.handle, .level = ::vkfu::enums::command_buffer_level::secondary, .command_buffer_count = 1 }, ::std::span{ &owner.handle, 1u });
	auto buffers = ::std::vector<buffer_type>{};
	auto const command = owner.handle;
	::vkfu::begin_command_buffer(command, ::vkfu::param::command_buffer_begin{
		.flags = {.one_time_submit = 1, .render_pass_continue = 1},
		.inheritance_info = ::vkfu::param::command_buffer_inheritance{}
			| ::vkfu::param::option::command_buffer_inheritance_rendering{
				.color_attachment_formats = ::std::array{ _impl->format }, .rasterization_samples = ::vkfu::enums::sample_count::count_1 }
	});
	::vkfu::cmd_set_viewport(command, 0, ::std::array{ ::vkfu::evaluate(::vkfu::param::viewport{
		.width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .max_depth = 1.0f }) });
	::vkfu::cmd_set_scissor(command, 0, ::std::array{ ::vkfu::evaluate(::vkfu::param::rect_2d{ .extent = extent }) });

	auto const scale = (::std::min)(static_cast<float>(extent.width) / 960.0f, static_cast<float>(extent.height) / 540.0f);
	auto const origin_x = (static_cast<float>(extent.width) - 960.0f * scale) * 0.5f;
	auto const origin_y = (static_cast<float>(extent.height) - 540.0f * scale) * 0.5f;
	auto rectangle = [&](float x, float y, float width, float height, ::glm::vec4 tint)
	{
		buffers.push_back(_impl->graph.draw_rectangle(_impl->device, _impl->physical_device, command, { 1, 1 }, { tint.r, tint.g, tint.b, tint.a }, ::make_transform(extent, origin_x + x * scale, origin_y + y * scale, width * scale, height * scale, { 1, 1 })));
	};
	auto label = [&](::std::size_t index, float x, float y, float height, ::glm::vec4 tint, bool centered = false)
	{
		auto const& text = _impl->labels.at(index);
		auto const size = text.extent();
		auto const width = height * static_cast<float>(size.width) / static_cast<float>(size.height);
		if (centered)
			x -= width * 0.5f;
		buffers.push_back(text.draw(_impl->device, _impl->physical_device, command,
			::make_transform(extent, origin_x + x * scale, origin_y + y * scale, width * scale, height * scale, size), tint));
	};
	auto const gold = ::glm::vec4{ 0.95f, 0.72f, 0.36f, 1.0f };
	auto const pale = ::glm::vec4{ 0.88f, 0.92f, 0.98f, 1.0f };
	auto const muted = ::glm::vec4{ 0.51f, 0.61f, 0.73f, 1.0f };
	buffers.push_back(_impl->graph.draw_rectangle(_impl->device, _impl->physical_device, command, { 1, 1 }, { 0.045f, 0.07f, 0.12f, 1.0f }, ::make_transform(extent, 0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), { 1, 1 })));
	rectangle(48, 45, 3, 450, gold);
	rectangle(660, 68, 230, 1, { 0.3f, 0.42f, 0.56f, 0.7f });
	rectangle(660, 472, 230, 1, { 0.3f, 0.42f, 0.56f, 0.7f });
	label(0, 764, 150, 110, pale, true);
	label(1, 764, 265, 19, gold, true);

	rectangle(88, 74, 480, 378, { 0.025f, 0.04f, 0.07f, 0.8f });
	label(0, 116, 93, 38, gold);
	constexpr auto count = ::std::size_t{ 2 };
	for (auto index = ::std::size_t{}; index < count; ++index)
	{
		auto const y = 164.0f + static_cast<float>(index) * 70.0f;
		auto const selected = _impl->state.selected == index;
		rectangle(112, y, 432, 54, selected ? gold : ::glm::vec4{ 0.17f, 0.24f, 0.33f, 0.35f });
		if (selected)
			rectangle(126, y + 23, 7, 7, { 0.06f, 0.08f, 0.12f, 1.0f });
		label(2 + index, 150, y + 10, 32, selected ? ::glm::vec4{ 0.06f, 0.08f, 0.12f, 1.0f } : pale);
	}
	label(4, 112, 393, 16, muted);
	label(5, 112, 417, 16, muted);
	::vkfu::end_command_buffer(command);
	return ::stdexec::just(::std::move(commands), ::std::move(buffers));
	});
}
