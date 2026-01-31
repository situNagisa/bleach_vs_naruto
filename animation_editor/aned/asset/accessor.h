#pragma once

#include "./library.h"

namespace aned::asset
{
	struct accessor
	{
		asset_library* _library{};
		asset_folder* _current_folder = &_library->_root;
		bool _emplace_if_not_exist = false;

		constexpr auto&& emplace_if() noexcept
		{
			_emplace_if_not_exist = true;
			return *this;
		}
		constexpr auto&& data() const noexcept { return _current_folder->children; }
		auto&& folder(::std::string_view name)
		{
			auto it = data().find(::std::string(name));
			asset_folder* folder{};
			if (it == data().end())
			{
				if (!_emplace_if_not_exist)
					throw ::std::runtime_error(::std::format("Asset {} not found", name));
				auto hive_it = _library->_folders.emplace();
				auto [new_it, insert] = _current_folder->children.emplace(::std::string(name), ::std::to_address(hive_it));
				BOOST_ASSERT(insert);
				it = new_it;
				folder = ::std::to_address(hive_it);
			}
			else
			{
				auto tmp = ::std::get_if<asset::asset_folder*>(&it->second);
				if (!tmp)
					throw ::std::runtime_error(::std::format("Asset {} is not folder", name));
				folder = *tmp;
			}
			_current_folder = folder;
			return *this;
		}
		auto&& image(::std::string_view name) const
		{
			auto it = data().find(::std::string(name));
			asset::image* i{};
			if (it == data().end())
			{
				if (!_emplace_if_not_exist)
					throw ::std::runtime_error(::std::format("Asset {} not found", name));
				auto hive_it = _library->_images.emplace();
				auto [new_it, insert] = _current_folder->children.emplace(::std::string(name), ::std::to_address(hive_it));
				BOOST_ASSERT(insert);
				it = new_it;
				i = ::std::to_address(hive_it);
			}
			else
			{
				auto tmp = ::std::get_if<asset::image*>(&it->second);
				if (!tmp)
					throw ::std::runtime_error(::std::format("Asset {} is not image", name));
				i = *tmp;
			}
			return *i;
		}
		auto&& movie_clip(::std::string_view name) const
		{
			auto it = data().find(::std::string(name));
			asset::movie_clip* mc{};
			if (it == data().end())
			{
				if (!_emplace_if_not_exist)
					throw ::std::runtime_error(::std::format("Asset {} not found", name));
				auto hive_it = _library->_movie_clips.emplace();
				auto [new_it, insert] = _current_folder->children.emplace(::std::string(name), ::std::to_address(hive_it));
				BOOST_ASSERT(insert);
				it = new_it;
				mc = ::std::to_address(hive_it);
			}
			else
			{
				auto tmp = ::std::get_if<asset::movie_clip*>(&it->second);
				if (!tmp)
					throw ::std::runtime_error(::std::format("Asset {} is not movie clip", name));
				mc = *tmp;
			}
			return *mc;
		}
	};
}