#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "./main_menu.h"
#include "./main_menu/background.h"
#include "./main_menu/text.h"
#include "./main_menu/vector_graph.h"
#include "./ui/ui.h"

namespace
{
	constexpr auto options = ::std::array<::std::string_view, main_menu::option_count>{ "START GAME", "SETTINGS", "EXIT" };
	constexpr auto exit_option = ::std::size_t{ 2 };
	constexpr auto panel_width = 340.0f;

	[[nodiscard]] auto layout_of(renderer const& draw) noexcept -> ui::menu_layout
	{
		return ui::menu_layout::compute(ui::viewport_of(draw), options.size(), panel_width);
	}
}

struct main_menu::implementation : immovable
{
	renderer& _renderer;
	state _state{};
	ui::key_repeat _navigation{};
	ui::fade _fade{};
	::std::optional<::std::size_t> _pending{};   ///< 已确认、等淡出结束再上报的选项

	background _background;
	text_type _text;
	vector_graph_type _vector_graph;

	::vkkl::command_pool _pool;

	implementation(renderer& r)
		: _renderer(r)
		, _background(
			::bvn::assets::load_image(resource_dir / "background.jpg")
			, r.vulkan.global_env().device()
			, r.vulkan.global_env().physical_device()
			, r.vulkan.global_env().graphics_queue()
			, r.vulkan.global_env().graphics_queue_family()
			, r.vulkan.global_env().swapchain_image_format()
			)
		, _text(
			::bvn::assets::load_image(resource_dir / "font.png")
			, r.vulkan.global_env().device()
			, r.vulkan.global_env().physical_device()
			, r.vulkan.global_env().graphics_queue()
			, r.vulkan.global_env().graphics_queue_family()
			, r.vulkan.global_env().swapchain_image_format()
		)
		, _vector_graph(r.vulkan.global_env().device(), r.vulkan.global_env().swapchain_image_format())
		, _pool(::consumer_arch_vulkan::create_secondary_command_pool(r.vulkan.global_env()))
	{}
};

main_menu::main_menu(renderer& r) : _impl(::std::make_unique<implementation>(r)) {}
main_menu::~main_menu() = default;

values_sender_type<main_menu::state, main_menu::delta> main_menu::update(app_state::menu_type& shared, frame_clock::frame_time const& time, input::state const& current, input::delta const& changes)
{
	return ::stdexec::just() | ::stdexec::let_value([&]
		{
			auto&& impl = *_impl;
			auto&& view = impl._state;
			auto result = delta{};
			auto const dt = ui::seconds(time);
			view.seconds += dt;

			auto const layout = ::layout_of(impl._renderer);
			auto const& keys = changes.keyboard;
			auto const& held = current.keyboard;

			// 淡出中不再接受输入：选项已经定了。
			if (!impl._fade.leaving())
			{
				// 键盘 / 滚轮换行
				auto const up = keys.key(input::key_code::up).pressed || keys.key(input::key_code::w).pressed;
				auto const down = keys.key(input::key_code::down).pressed || keys.key(input::key_code::s).pressed;
				auto const holding = static_cast<int>(held.key(input::key_code::down) || held.key(input::key_code::s))
					- static_cast<int>(held.key(input::key_code::up) || held.key(input::key_code::w));
				auto direction = impl._navigation.step(time.now, up, down, holding);
				if (changes.mouse.wheel.y > 0.0f)
					direction = -1;
				else if (changes.mouse.wheel.y < 0.0f)
					direction = 1;
				if (direction != 0)
				{
					view.selected = (view.selected + options.size() + static_cast<::std::size_t>(direction + 1) - 1) % options.size();
					result.moved = true;
				}

				// 鼠标：动了才改选中项，不然键盘选完会被静止的指针抢回去。
				view.hovered = layout.hit(ui::mouse_of(current));
				auto const mouse_moved = changes.mouse.motion.x != 0.0f || changes.mouse.motion.y != 0.0f;
				if (mouse_moved && view.hovered && *view.hovered != view.selected)
				{
					view.selected = *view.hovered;
					result.moved = true;
				}

				auto activate = keys.key(input::key_code::enter).pressed
					|| keys.key(input::key_code::keypad_enter).pressed
					|| keys.key(input::key_code::space).pressed
					|| (changes.mouse.button(input::mouse_button::left).pressed && view.hovered == view.selected && view.hovered.has_value());

				// Esc：先跳到 EXIT，再按一次才真的退出。
				if (keys.key(input::key_code::escape).pressed)
				{
					if (view.selected == exit_option)
						activate = true;
					else
					{
						view.selected = exit_option;
						result.moved = true;
					}
				}

				if (result.moved)
					impl._navigation.rearm(time.now);

				if (activate)
				{
					impl._pending = view.selected;
					view.press[view.selected] = 1.0f;
					impl._fade.leave();
				}
			}

			// 淡出走完的那一帧才把选项交出去：main 在帧末读到它就切场景。
			if (impl._fade.advance(dt))
				result.activated = impl._pending;
			shared.activated = result.activated;   // 本帧输出：整体覆盖

			view.overlay = impl._fade.overlay();
			view.cursor = ui::approach(view.cursor, static_cast<float>(view.selected), 18.0f, dt);
			for (auto&& press : view.press)
				press = ui::approach(press, 0.0f, 9.0f, dt);

			return ::stdexec::just(view, ::std::move(result));
		});
}

values_sender_type<main_menu::recording> main_menu::record(frame_slot&, state const& snapshot)
{
	return ::stdexec::just() | ::stdexec::let_value([&]
		{
			auto&& impl = *_impl;
			auto const& view = snapshot;
			auto command = ui::begin_secondary(impl._renderer, impl._pool);
			auto buffers = ::std::vector<buffer_type>{};
			buffers.reserve(24);
			auto paint = ui::painter{ impl._renderer, command.handle, impl._vector_graph, impl._text, buffers };

			auto const layout = ::layout_of(impl._renderer);
			auto const sc = layout.scale;
			auto const viewport = layout.viewport;
			auto const t = view.seconds;

			// 背景：铺满并缓慢漂移（Ken Burns）。
			{
				auto const image = ::glm::vec2{
					static_cast<float>(impl._background.texture.source.width),
					static_cast<float>(impl._background.texture.source.height)
				};
				auto const cover = (::std::max)(viewport.x / image.x, viewport.y / image.y) * (1.06f + 0.015f * ::std::sin(t * 0.3f));
				auto const drift = ::glm::vec2{ 14.0f * ::std::sin(t * 0.21f), 9.0f * ::std::cos(t * 0.17f) } * sc;
				auto const offset = (::glm::vec2{ viewport.x, viewport.y } - image * cover) * 0.5f + drift;
				buffers.emplace_back(impl._background.draw(paint.device, paint.physical_device, command.handle,
					paint.transform * ::glm::scale(::glm::translate(::glm::mat3{ 1.0f }, offset), ::glm::vec2{ cover })));
			}
			paint.fill(layout.full(), ui::palette::shade);
			// 上下暗带，电影感。
			paint.fill({ 0.0f, 0.0f, viewport.x, 56.0f * sc }, ui::with_alpha(ui::palette::black, 0.35f));
			paint.fill({ 0.0f, viewport.y - 56.0f * sc, viewport.x, 56.0f * sc }, ui::with_alpha(ui::palette::black, 0.35f));

			// 面板
			auto const panel = layout.panel(118.0f * sc, 84.0f * sc);
			paint.fill(panel, ui::palette::panel);
			paint.outline(panel, 1.0f * sc, ui::palette::panel_edge);
			paint.fill({ panel.left, panel.top, panel.width, 3.0f * sc }, ui::palette::gold);

			// 标题：轻微脉动
			auto const center_x = panel.center().x;
			auto const pulse = 1.0f + 0.025f * ::std::sin(t * 2.2f);
			paint.label("BVN", 64.0f * sc * pulse, { center_x, layout.top - 70.0f * sc }, 0, ui::palette::gold);
			paint.label("BLEACH VS NARUTO", 15.0f * sc, { center_x, layout.top - 30.0f * sc }, 0, ui::palette::muted, panel.width - 40.0f * sc);
			paint.fill({ center_x - 60.0f * sc, layout.top - 16.0f * sc, 120.0f * sc, 1.0f * sc }, ui::palette::panel_edge);

			// 高亮条跟着 cursor 缓动，文字按真正的 selected 着色。
			paint.fill(layout.row(view.cursor), ui::palette::gold);
			for (auto index = ::std::size_t{}; index < options.size(); ++index)
			{
				auto const row = layout.row(index);
				auto const selected = index == view.selected;
				if (!selected)
					paint.fill(row, view.hovered == index ? ui::palette::hover : ui::palette::row);
				if (view.press[index] > 0.01f)
					paint.fill(row, ui::with_alpha(ui::palette::white, 0.6f * view.press[index]));
				paint.label(options[index], 26.0f * sc, row.center(), 0, selected ? ui::palette::ink : ui::palette::paper, row.width - 72.0f * sc);
				if (selected)
				{
					// 左侧的箭头来回轻晃
					auto const bob = 3.0f * sc * ::std::sin(t * 6.0f);
					paint.label(">", 24.0f * sc, { row.left + 24.0f * sc + bob, row.center().y }, 0, ui::palette::ink);
				}
			}

			// 页脚提示与版本
			paint.label("UP / DOWN  SELECT      ENTER  CONFIRM      ESC  EXIT", 13.0f * sc, { center_x, layout.bottom() + 40.0f * sc }, 0, ui::palette::muted, panel.width - 32.0f * sc);
			paint.label("DEMO BUILD 0.1", 12.0f * sc, { viewport.x - 18.0f * sc, viewport.y - 18.0f * sc }, 1, ui::with_alpha(ui::palette::muted, 0.8f));

			// 淡入淡出
			paint.fill(layout.full(), ui::with_alpha(ui::palette::black, view.overlay));

			::consumer_arch_vulkan::check(::vkEndCommandBuffer(command.handle), "main menu: end draw");
			return ::stdexec::just(recording{ .command = ::std::move(command), .buffers = ::std::move(buffers) });
		});
}
