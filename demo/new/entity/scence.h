#pragma once

#include "./render.h"
#include "./input.h"
#include "./main_menu.h"
#include "./setting_scene.h"
#include "./fighting_scene.h"

struct exit_scene {};


struct scene_type
{
	struct exit {};
	struct open_settings {};
	struct start_fighting {};
	struct back_to_menu {};

	struct machine
	{
		auto operator()() const noexcept {
			using ::boost::sml::state;
			using ::boost::sml::event;
			using ::boost::sml::on_entry;
			using ::boost::sml::on_exit;
			using ::boost::sml::_;
			return ::boost::sml::make_transition_table(
				*state<main_menu> +on_entry<_> / [](scene_type& entitites) { ::std::construct_at(&entitites.current_state.menu, entitites.renderer); }
			, state<main_menu> +on_exit<_> / [](scene_type& entitites) noexcept { ::std::destroy_at(&entitites.current_state.menu); }
				, state<main_menu> +event<exit> = state<exit_scene>
				, state<main_menu> +event<open_settings> = state<setting_scene>
				, state<main_menu> +event<start_fighting> = state<fighting_scene>

				, state<setting_scene> +on_entry<_> / [](scene_type& entitites) { ::std::construct_at(&entitites.current_state.settings, entitites.renderer); }
			, state<setting_scene> +on_exit<_> / [](scene_type& entitites) noexcept { ::std::destroy_at(&entitites.current_state.settings); }
				, state<setting_scene> +event<back_to_menu> = state<main_menu>
				, state<setting_scene> +event<exit> = state<exit_scene>

				, state<fighting_scene> +on_entry<_> / [](scene_type& entitites) { ::std::construct_at(&entitites.current_state.fighting, entitites.input, entitites.renderer); }
			, state<fighting_scene> +on_exit<_> / [](scene_type& entitites) noexcept { ::std::destroy_at(&entitites.current_state.fighting); }
				, state<fighting_scene> +event<back_to_menu> = state<main_menu>
				, state<fighting_scene> +event<exit> = state<exit_scene>
				);
		}
	};

	renderer& renderer;
	input& input;
	union scene_state_type
	{
		exit_scene exit{};
		main_menu menu;
		fighting_scene fighting;
		setting_scene settings;
	} current_state{};
	::boost::sml::sm<machine> sm{ *this };
};