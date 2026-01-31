#pragma once

#include <map>
#include <string>
#include <stdexcept>
#include <variant>
#include "hive.h"

#include <boost/assert.hpp>

#include "./image.h"
#include "./movie_clip.h"

namespace aned::asset
{
	struct asset_folder
	{
		::std::map<
			::std::string
		, ::std::variant<
			image*
			, movie_clip*
			, asset_folder*
			>
		> children{};
	};

	struct asset_library
	{
		::plf::hive<image> _images{};
		::plf::hive<movie_clip> _movie_clips{};
		::plf::hive<asset_folder> _folders{};
		asset_folder _root{};

		auto&& root() noexcept { return _root; }
		auto&& root() const noexcept { return _root; }
	};
}