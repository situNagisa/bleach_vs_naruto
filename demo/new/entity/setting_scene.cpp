#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include "./setting_scene.h"
#include "./main_menu/background.h"
#include "./main_menu/text.h"
#include "./main_menu/vector_graph.h"
#include "./ui/ui.h"

#include "./setting_scene/settings.h"

namespace
{
	enum row : ::std::size_t
	{
		volume,
		fullscreen,
		frame_rate,
		reset,
		back,
	};
	static_assert(row::back + 1 == setting_scene::row_count);

	constexpr auto labels = ::std::array<::std::string_view, setting_scene::row_count>{ "VOLUME", "FULLSCREEN", "FPS LIMIT", "RESET TO DEFAULTS", "BACK" };
	constexpr auto panel_width = 520.0f;
	constexpr auto volume_step = 5;

	[[nodiscard]] auto layout_of(renderer const& draw) noexcept -> ui::menu_layout
	{
		return ui::menu_layout::compute(ui::viewport_of(draw), setting_scene::row_count, panel_width);
	}

	/// 一行里的控件位置。update 用来命中，record 用来画，两边一致。
	struct row_widgets
	{
		ui::rect value;    ///< 行右侧放值的区域
		ui::rect slider;   ///< 音量滑轨
		ui::rect toggle;   ///< 全屏开关
	};

	[[nodiscard]] auto widgets_of(ui::menu_layout const& layout, ::std::size_t index) noexcept -> row_widgets
	{
		auto const r = layout.row(index);
		auto const sc = layout.scale;
		auto const value = ui::rect{ r.left + r.width * 0.46f, r.top, r.width * 0.54f - 20.0f * sc, r.height };
		auto const center_y = r.center().y;
		return {
			.value = value,
			.slider = { value.left, center_y - 5.0f * sc, value.width - 78.0f * sc, 10.0f * sc },
			.toggle = { value.right() - 64.0f * sc, center_y - 14.0f * sc, 64.0f * sc, 28.0f * sc },
		};
	}

	[[nodiscard]] auto cycle_frame_rate(int current, int direction) noexcept -> int
	{
		auto const& rates = game_settings::frame_rates;
		auto const found = ::std::ranges::find(rates, current);
		auto const index = static_cast<int>(found == rates.end() ? 0 : found - rates.begin());
		auto const count = static_cast<int>(rates.size());
		return rates[static_cast<::std::size_t>((index + (direction < 0 ? -1 : 1) + count) % count)];
	}

	[[nodiscard]] auto frame_rate_text(int frame_rate) -> ::std::string
	{
		return frame_rate == 0 ? ::std::string{ "UNLIMITED" } : ::std::to_string(frame_rate);
	}
}

struct setting_scene::implementation : immovable
{
	renderer& _renderer;
	state _state{};
	ui::key_repeat _navigation{};
	ui::key_repeat _adjust{};
	ui::fade _fade{};
	game_settings _entry{};   ///< 进入本场景时的偏好，用来显示"有改动"
	bool _entered = false;    ///< 这个 entity 跨场景切换一直活着，所以每次回来要重新"进入"

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
	{
	}

	void enter(game_settings const& preferences)
	{
		_entered = true;
		_entry = preferences;
		_fade.reset();
		_navigation = {};
		_adjust = {};
		_state = state{};
		_state.shown = preferences;
		_state.volume_knob = static_cast<float>(preferences.volume) / 100.0f;
		_state.toggle_knob = preferences.fullscreen ? 1.0f : 0.0f;
	}
};

setting_scene::setting_scene(renderer& r) : _impl(::std::make_unique<implementation>(r)) {}
setting_scene::~setting_scene() = default;

values_sender_type<setting_scene::state, setting_scene::delta> setting_scene::update(app_state::settings_type& shared, frame_clock::frame_time const& time, input::state const& current, input::delta const& changes)
{
	return ::stdexec::just() | ::stdexec::let_value([&]
		{
			auto&& impl = *_impl;
			if (!impl._entered)
				impl.enter(shared.preferences);
			auto&& view = impl._state;
			auto&& preferences = shared.preferences;
			auto const before = preferences;
			auto result = delta{};
			auto const dt = ui::seconds(time);
			view.seconds += dt;

			auto const layout = ::layout_of(impl._renderer);
			auto const& keys = changes.keyboard;
			auto const& held = current.keyboard;

			if (!impl._fade.leaving())
			{
				// 上下换行
				auto const up = keys.key(input::key_code::up).pressed || keys.key(input::key_code::w).pressed;
				auto const down = keys.key(input::key_code::down).pressed || keys.key(input::key_code::s).pressed;
				auto const holding = static_cast<int>(held.key(input::key_code::down) || held.key(input::key_code::s))
					- static_cast<int>(held.key(input::key_code::up) || held.key(input::key_code::w));
				auto const direction = impl._navigation.step(time.now, up, down, holding);
				if (direction != 0)
				{
					view.selected = (view.selected + row_count + static_cast<::std::size_t>(direction + 1) - 1) % row_count;
					result.moved = true;
				}

				// 鼠标悬停换行（指针动了才算）
				auto const mouse = ui::mouse_of(current);
				view.hovered = layout.hit(mouse);
				auto const mouse_moved = changes.mouse.motion.x != 0.0f || changes.mouse.motion.y != 0.0f;
				if (mouse_moved && view.hovered && *view.hovered != view.selected)
				{
					view.selected = *view.hovered;
					result.moved = true;
				}

				// 左右 / 滚轮调值
				auto const left = keys.key(input::key_code::left).pressed || keys.key(input::key_code::a).pressed;
				auto const right = keys.key(input::key_code::right).pressed || keys.key(input::key_code::d).pressed;
				auto const holding_adjust = static_cast<int>(held.key(input::key_code::right) || held.key(input::key_code::d))
					- static_cast<int>(held.key(input::key_code::left) || held.key(input::key_code::a));
				if (result.moved)
				{
					impl._navigation.rearm(time.now);
					impl._adjust.rearm(time.now);
				}
				auto adjust = impl._adjust.step(time.now, left, right, holding_adjust);
				if (changes.mouse.wheel.y != 0.0f)
					adjust = changes.mouse.wheel.y > 0.0f ? 1 : -1;

				auto activate = keys.key(input::key_code::enter).pressed
					|| keys.key(input::key_code::keypad_enter).pressed
					|| keys.key(input::key_code::space).pressed;

				// 鼠标点击：滑块按位置定值，其余行等同确认。
				if (changes.mouse.button(input::mouse_button::left).pressed && view.hovered)
				{
					view.selected = *view.hovered;
					auto const widgets = ::widgets_of(layout, view.selected);
					if (view.selected == row::volume && widgets.slider.inset(-8.0f * layout.scale, -10.0f * layout.scale).contains(mouse))
					{
						auto const fraction = ui::clamp01((mouse.x - widgets.slider.left) / widgets.slider.width);
						preferences.volume = static_cast<int>(::std::lround(fraction * 100.0f / volume_step)) * volume_step;
					}
					else if (view.selected != row::volume)
						activate = true;
				}

				switch (view.selected)
				{
				case row::volume:
					if (adjust != 0)
						preferences.volume = ::std::clamp(preferences.volume + adjust * volume_step, 0, 100);
					break;
				case row::fullscreen:
					if (adjust != 0 || activate)
						preferences.fullscreen = !preferences.fullscreen;
					break;
				case row::frame_rate:
					if (adjust != 0 || activate)
						preferences.frame_rate = ::cycle_frame_rate(preferences.frame_rate, adjust != 0 ? adjust : 1);
					break;
				case row::reset:
					if (activate)
						preferences = game_settings{};
					break;
				case row::back:
					if (activate)
						impl._fade.leave();
					break;
				default:
					break;
				}

				if (keys.key(input::key_code::escape).pressed)
					impl._fade.leave();
			}

			// 变过的行闪一下
			if (preferences.volume != before.volume)
				view.flash[row::volume] = 1.0f;
			if (preferences.fullscreen != before.fullscreen)
				view.flash[row::fullscreen] = 1.0f;
			if (preferences.frame_rate != before.frame_rate)
				view.flash[row::frame_rate] = 1.0f;
			if (preferences != before && view.selected == row::reset)
				view.flash[row::reset] = 1.0f;
			result.changed = preferences != before;

			// 淡出走完的那一帧才通知 main；main 会保存并切回菜单，下次进来重新淡入。
			if (impl._fade.advance(dt))
			{
				result.back_requested = true;
				view.flash[row::back] = 1.0f;
				impl._entered = false;
			}
			shared.back_requested = result.back_requested;   // 本帧输出：整体覆盖

			view.shown = preferences;
			view.dirty = preferences != impl._entry;
			view.overlay = impl._fade.overlay();
			view.cursor = ui::approach(view.cursor, static_cast<float>(view.selected), 18.0f, dt);
			view.volume_knob = ui::approach(view.volume_knob, static_cast<float>(preferences.volume) / 100.0f, 16.0f, dt);
			view.toggle_knob = ui::approach(view.toggle_knob, preferences.fullscreen ? 1.0f : 0.0f, 18.0f, dt);
			for (auto&& flash : view.flash)
				flash = ui::approach(flash, 0.0f, 8.0f, dt);

			return ::stdexec::just(view, ::std::move(result));
		});
}

values_sender_type<setting_scene::recording> setting_scene::record(frame_slot&, state const& snapshot)
{
	return ::stdexec::just() | ::stdexec::let_value([&]
		{
			auto&& impl = *_impl;
			auto const& view = snapshot;
			auto command = ui::begin_secondary(impl._renderer, impl._pool);
			auto buffers = ::std::vector<buffer_type>{};
			buffers.reserve(40);
			auto paint = ui::painter{ impl._renderer, command.handle, impl._vector_graph, impl._text, buffers };

			auto const layout = ::layout_of(impl._renderer);
			auto const sc = layout.scale;
			auto const viewport = layout.viewport;

			// 背景：铺满、稍微放大，比主菜单更暗更静。
			{
				auto const image = ::glm::vec2{
					static_cast<float>(impl._background.texture.source.width),
					static_cast<float>(impl._background.texture.source.height)
				};
				auto const cover = (::std::max)(viewport.x / image.x, viewport.y / image.y) * 1.04f;
				auto const offset = (::glm::vec2{ viewport.x, viewport.y } - image * cover) * 0.5f;
				buffers.emplace_back(impl._background.draw(paint.device, paint.physical_device, command.handle,
					paint.transform * ::glm::scale(::glm::translate(::glm::mat3{ 1.0f }, offset), ::glm::vec2{ cover })));
			}
			paint.fill(layout.full(), ui::with_alpha(ui::palette::shade, 0.6f));

			// 面板与标题
			auto const panel = layout.panel(96.0f * sc, 96.0f * sc);
			paint.fill(panel, ui::palette::panel);
			paint.outline(panel, 1.0f * sc, ui::palette::panel_edge);
			paint.fill({ panel.left, panel.top, panel.width, 3.0f * sc }, ui::palette::gold);
			auto const center_x = panel.center().x;
			paint.label("SETTINGS", 40.0f * sc, { center_x, layout.top - 52.0f * sc }, 0, ui::palette::gold);
			paint.fill({ center_x - 80.0f * sc, layout.top - 22.0f * sc, 160.0f * sc, 1.0f * sc }, ui::palette::panel_edge);

			// 高亮：半透明金底 + 左侧金条，跟着 cursor 缓动
			{
				auto const bar = layout.row(view.cursor);
				paint.fill(bar, ui::palette::gold_dim);
				paint.fill({ bar.left, bar.top, 4.0f * sc, bar.height }, ui::palette::gold);
			}

			auto const& shown = view.shown;
			for (auto index = ::std::size_t{}; index < row_count; ++index)
			{
				auto const r = layout.row(index);
				auto const widgets = ::widgets_of(layout, index);
				auto const selected = index == view.selected;
				auto const text_color = selected ? ui::palette::gold : ui::palette::paper;
				auto const center_y = r.center().y;

				if (!selected)
					paint.fill(r, view.hovered == index ? ui::palette::hover : ui::palette::row);
				if (view.flash[index] > 0.01f)
					paint.fill(r, ui::with_alpha(ui::palette::white, 0.35f * view.flash[index]));

				switch (index)
				{
				case row::volume:
				{
					paint.label(labels[index], 22.0f * sc, { r.left + 20.0f * sc, center_y }, -1, text_color);
					auto const& slider = widgets.slider;
					paint.fill(slider, ui::with_alpha(ui::palette::white, 0.14f));
					paint.fill({ slider.left, slider.top, slider.width * view.volume_knob, slider.height }, ui::palette::gold);
					auto const knob = 18.0f * sc;
					paint.fill({ slider.left + slider.width * view.volume_knob - knob * 0.5f, center_y - knob * 0.5f, knob, knob }, selected ? ui::palette::white : ui::palette::paper);
					paint.label(::std::to_string(shown.volume) + "%", 22.0f * sc, { widgets.value.right(), center_y }, 1, text_color);
					break;
				}
				case row::fullscreen:
				{
					paint.label(labels[index], 22.0f * sc, { r.left + 20.0f * sc, center_y }, -1, text_color);
					auto const& toggle = widgets.toggle;
					paint.fill(toggle, ui::mix(ui::with_alpha(ui::palette::white, 0.15f), ui::palette::gold, view.toggle_knob));
					auto const knob = toggle.height - 6.0f * sc;
					paint.fill({ toggle.left + 3.0f * sc + (toggle.width - knob - 6.0f * sc) * view.toggle_knob, toggle.top + 3.0f * sc, knob, knob }, ui::palette::white);
					paint.label(shown.fullscreen ? "ON" : "OFF", 22.0f * sc, { toggle.left - 14.0f * sc, center_y }, 1, text_color);
					break;
				}
				case row::frame_rate:
				{
					paint.label(labels[index], 22.0f * sc, { r.left + 20.0f * sc, center_y }, -1, text_color);
					auto const arrow = selected ? ui::palette::gold : ui::palette::muted;
					paint.label("<", 22.0f * sc, { widgets.value.left + 12.0f * sc, center_y }, 0, arrow);
					paint.label(::frame_rate_text(shown.frame_rate), 22.0f * sc, { widgets.value.center().x, center_y }, 0, text_color, widgets.value.width - 60.0f * sc);
					paint.label(">", 22.0f * sc, { widgets.value.right() - 12.0f * sc, center_y }, 0, arrow);
					break;
				}
				case row::reset:
					paint.label(labels[index], 22.0f * sc, r.center(), 0, text_color, r.width - 40.0f * sc);
					break;
				case row::back:
					paint.label(view.dirty ? "SAVE AND BACK" : labels[index], 22.0f * sc, r.center(), 0, text_color, r.width - 40.0f * sc);
					break;
				default:
					break;
				}
			}

			// 页脚：操作提示 + 保存状态
			paint.label("UP / DOWN  SELECT     LEFT / RIGHT  ADJUST     ENTER  TOGGLE     ESC  BACK", 13.0f * sc, { center_x, layout.bottom() + 36.0f * sc }, 0, ui::palette::muted, panel.width - 32.0f * sc);
			paint.label(view.dirty ? "UNSAVED CHANGES - SAVED WHEN YOU GO BACK" : "ALL CHANGES SAVED", 13.0f * sc, { center_x, layout.bottom() + 62.0f * sc }, 0, view.dirty ? ui::palette::gold : ui::palette::muted, panel.width - 32.0f * sc);

			// 淡入淡出
			paint.fill(layout.full(), ui::with_alpha(ui::palette::black, view.overlay));

			::consumer_arch_vulkan::check(::vkEndCommandBuffer(command.handle), "settings: end draw");
			return ::stdexec::just(recording{ .command = ::std::move(command), .buffers = ::std::move(buffers) });
		});
}
