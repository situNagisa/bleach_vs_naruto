#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_loadso.h>

#include "./player_sdk.h"

/// 宿主侧：一个已加载的 player 插件。
///
/// **卸载顺序**：这个对象必须比它造出来的所有 player 活得久——player 的虚表、析构函数、
/// 它 frame 里实例化的 operation_state 代码全在 dll 里。场景用 `shared_ptr` 持有，
/// 并把它声明在 player 之前（后声明先析构），见 fighting_scene::plugin_player。
/// 加载/卸载只在帧边界做，那时没有任何 operation_state 活着。
struct player_library
{
	::SDL_SharedObject* _handle = nullptr;
	::player_module const* _module = nullptr;

	explicit player_library(::std::filesystem::path const& path)
		: _handle(::SDL_LoadObject(path.string().c_str()))
	{
		if (_handle == nullptr)
			throw ::std::runtime_error{ "player plugin: " + path.string() + ": " + ::SDL_GetError() };

		using entry_type = auto (*)() noexcept -> ::player_module const*;
		auto const entry = reinterpret_cast<entry_type>(::SDL_LoadFunction(_handle, "bvn_player_module"));
		if (entry == nullptr || (_module = entry()) == nullptr || _module->sdk_version != ::player_sdk_version)
		{
			::SDL_UnloadObject(_handle);
			throw ::std::runtime_error{ "player plugin: " + path.string() + ": SDK 不匹配" };
		}
	}

	player_library(player_library&&) = delete;
	auto operator=(player_library&&) -> player_library& = delete;

	~player_library()
	{
		::SDL_UnloadObject(_handle);
	}

	[[nodiscard]] auto create(::player_spawn spawn) const -> ::std::unique_ptr<::dynamic_player>
	{
		return _module->create(spawn);
	}
};
