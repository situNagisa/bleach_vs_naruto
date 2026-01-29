#pragma once

#include <map>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <generator>

#include <boost/assert.hpp>

#include <entt/entt.hpp>

#include "./image.h"
#include "./movie_clip.h"

namespace aned::component
{
	struct asset_folder
	{
		::std::map<::std::string, ::entt::entity> children{};
	};
}

namespace aned::asset
{
	struct asset_library
	{
		::entt::registry _registry{};
		::entt::entity _root{_registry.create()};

		asset_library()
		{
			_registry.emplace<component::asset_folder>(_root);
		}

		auto create_folder(::std::filesystem::path const& path)
		{
			auto current = _root;

			for (auto&& part : path)
			{
				if (part == path.root_path())
					continue;

				auto name = part.string();

				BOOST_ASSERT(_registry.valid(current));
				BOOST_ASSERT(_registry.any_of<component::asset_folder>(current));
				auto&& folder = _registry.get<component::asset_folder>(current);

				auto it = folder.children.find(name);
				if (it != folder.children.end())
				{
					current = it->second;
					if (!_registry.any_of<component::asset_folder>(current))
						throw ::std::runtime_error(::std::format("{} is not directory", part));
					continue;
				}
				auto e = _registry.create();
				_registry.emplace<component::asset_folder>(e);

				folder.children.emplace(name, e);

				current = e;
			}

			return ::entt::handle(_registry, current);
		}

		auto create(::std::filesystem::path const& path)
		{
			auto&& folder = create_folder(path.parent_path()).get<component::asset_folder>();
			if (folder.children.contains(path.filename().string()))
				throw ::std::runtime_error(::std::format("{} already exists", path));
			auto e = _registry.create();
			folder.children.emplace(path.filename().string(), e);
			return ::entt::handle(_registry, e);
		}

		auto create_image(::std::filesystem::path const& path, component::image image)
		{
			auto handle = create(path);
			handle.emplace<component::image>(::std::move(image));
			return handle;
		}
		auto create_movie_clip(::std::filesystem::path const& path, component::movie_clip movie_clip)
		{
			auto handle = create(path);
			handle.emplace<component::movie_clip>(::std::move(movie_clip));
			return handle;
		}
	};
}