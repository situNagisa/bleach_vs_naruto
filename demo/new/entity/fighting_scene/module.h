#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_loadso.h>

#include "./entity.h"

/// entity 模块（dll / so）。宿主和模块共享头文件、同一套工具链，C++ 类型直接过边界。
/// 一个模块可以导出多种 entity，按 kind 区分。

inline constexpr ::std::uint32_t entity_sdk_version = 1;

struct entity_module_api
{
	::std::uint32_t sdk_version;
	/// 不认识的 kind 返回 nullptr。
	auto (*create)(::std::string_view kind, entity_context const& context) -> ::std::unique_ptr<fighting_entity>;
};

#if defined(_WIN32)
#	define BVN_ENTITY_EXPORT __declspec(dllexport)
#else
#	define BVN_ENTITY_EXPORT __attribute__((visibility("default")))
#endif

/// 模块里写一行：`BVN_ENTITY_MODULE(create)`，`create` 的签名同 `entity_module_api::create`。
/// `extern "C"` 只为了符号名不被 mangle、能按名字找到；返回的是 C++ 结构。
#define BVN_ENTITY_MODULE(create_function)                                                          \
	extern "C" BVN_ENTITY_EXPORT auto bvn_entity_module() noexcept -> ::entity_module_api const*  \
	{                                                                                                \
		static constexpr auto api = ::entity_module_api{                                             \
			.sdk_version = ::entity_sdk_version,                                                     \
			.create = &create_function,                                                              \
		};                                                                                           \
		return &api;                                                                                 \
	}

/// 平台相关的模块文件名：`dir / name.dll` 或 `dir / libname.so`。
[[nodiscard]] inline auto module_path(::std::filesystem::path const& directory, ::std::string_view name) -> ::std::filesystem::path
{
#if defined(_WIN32)
	return directory / (::std::string{ name } + ".dll");
#else
	return directory / ("lib" + ::std::string{ name } + ".so");
#endif
}

struct loaded_module
{
	::SDL_SharedObject* _handle = nullptr;
	entity_module_api const* _api = nullptr;

	explicit loaded_module(::std::filesystem::path const& path)
		: _handle(::SDL_LoadObject(path.string().c_str()))
	{
		if (_handle == nullptr)
			throw ::std::runtime_error{ "entity module: " + path.string() + ": " + ::SDL_GetError() };
		using entry_type = auto (*)() noexcept -> entity_module_api const*;
		auto const entry = reinterpret_cast<entry_type>(::SDL_LoadFunction(_handle, "bvn_entity_module"));
		if (entry == nullptr || (_api = entry()) == nullptr || _api->sdk_version != entity_sdk_version)
		{
			::SDL_UnloadObject(_handle);
			throw ::std::runtime_error{ "entity module: " + path.string() + ": SDK 不匹配" };
		}
	}

	loaded_module(loaded_module&&) = delete;
	auto operator=(loaded_module&&) -> loaded_module& = delete;

	~loaded_module() { ::SDL_UnloadObject(_handle); }

	[[nodiscard]] auto create(::std::string_view kind, entity_context const& context) const -> ::std::unique_ptr<fighting_entity>
	{
		return _api->create(kind, context);
	}
};

/// 全局的模块缓存，main 持有（跟 renderer 同级），比所有场景活得久。
///
/// entity 持有它所在模块的 shared_ptr；缓存自己也握一份。entity 析构不会立刻卸载 dll——
/// 本帧的 operation_state（代码在 dll 里）可能还活着。卸载只在 `collect` 里做，main 在
/// 每帧开头调用：那时上一帧的 sync_wait 已经返回，operation_state 全部析构了。
struct module_cache
{
	::std::vector<::std::pair<::std::filesystem::path, ::std::shared_ptr<loaded_module>>> _modules{};

	[[nodiscard]] auto load(::std::filesystem::path const& path) -> ::std::shared_ptr<loaded_module>
	{
		for (auto const& [loaded, module] : _modules)
		{
			if (loaded == path)
				return module;
		}
		return _modules.emplace_back(path, ::std::make_shared<loaded_module>(path)).second;
	}

	/// 卸载没有 entity 在用的模块。
	void collect()
	{
		::std::erase_if(_modules, [](auto const& entry) { return entry.second.use_count() == 1; });
	}
};
