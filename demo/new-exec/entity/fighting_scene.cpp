#include <algorithm>

#include "./fighting_scene.h"

fighting_scene::fighting_scene(input&, renderer& r)
	: _renderer(&r)
{
	// 开场：naruto vs ichigo，两个都从插件目录加载。instance 先分配，所以互相当对手不用等加载。
	auto const root = ::std::filesystem::path{ "plugins" };
	auto const naruto = _next_instance;
	auto const ichigo = naruto + 1;
	request_spawn(root / "naruto" / "naruto.dll", ichigo);
	request_spawn(root / "ichigo" / "ichigo.dll", naruto);
	apply_structural_changes();
}

auto fighting_scene::_load(::std::filesystem::path const& path) -> ::std::shared_ptr<player_library>
{
	::std::erase_if(_libraries, [](auto const& entry) { return entry.second.expired(); });
	for (auto const& [loaded_path, library] : _libraries)
	{
		if (loaded_path == path)
			return library.lock();
	}
	auto library = ::std::make_shared<player_library>(path);
	_libraries.emplace_back(path, library);
	return library;
}

void fighting_scene::apply_structural_changes()
{
	if (_pending_spawns.empty())
		return;

	for (auto&& [path, instance, opponent] : ::std::exchange(_pending_spawns, {}))
	{
		auto library = _load(path);
		auto player = library->create(player_spawn{ .instance = instance, .opponent = opponent, .draw = _renderer });
		_players.push_back({ ::std::move(library), ::std::move(player) });
	}

	// 两份按 instance 下标的存储只在这里扩容；扩容让 span 失效，所以重新绑定。
	if (_registry_storage.size() < _next_instance)
	{
		_registry_storage.resize(_next_instance);
		_registry.rebind(_registry_storage);
		_command_storage.resize(_next_instance);
		_commands = command_list{ ._slots = _command_storage };
	}
}
