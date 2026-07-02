#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace bvn::assets
{
/**
 * A loaded, file-watched asset: the value plus a revision that increments every time
 * the backing file changes on disk and reloads successfully, and the last load error
 * (empty while `value` is current). Consumers hold a handle and compare `revision`
 * against their own cached copy to decide cheaply whether they must rebuild downstream
 * state (e.g. re-upload a GPU texture) — see hero hot-reload.
 */
template <class asset_type>
struct asset
{
	asset_type value = {};
	::std::uint64_t revision = 0;
	::std::string error;
};

template <class asset_type>
using asset_handle = ::std::shared_ptr<asset<asset_type> const>;

/**
 * Loads assets of type `asset_type` from disk, caches them by path (load once, share
 * the same handle for repeated requests), and hot-reloads changed files on poll().
 * Loading is synchronous; an async variant returning a sender can layer on top later
 * (engine-spec §4.7).
 *
 * Threading: poll() mutates cached values, so call it from one place (the main loop)
 * before handing the frame to the render thread — never concurrently with handle reads.
 */
template <class asset_type>
struct resource_cache
{
	using load_function = ::std::function<asset_type(::std::filesystem::path const&)>;

	explicit resource_cache(load_function loader)
		: load_asset(::std::move(loader))
	{}

	auto load(::std::filesystem::path const& path) -> asset_handle<asset_type>
	{
		auto key = path.lexically_normal().generic_string();

		if (auto found = entries.find(key); found != entries.end())
		{
			return found->second.slot;
		}

		auto created = entry{};
		created.path = path;
		created.slot = ::std::make_shared<asset<asset_type>>();

		//+ capture file identity
		{
			auto error = ::std::error_code{};
			if (::std::filesystem::exists(path, error) && !error)
			{
				created.last_identity.last_write_time = ::std::filesystem::last_write_time(path, error);
				created.last_identity.file_size = ::std::filesystem::file_size(path, error);
				created.last_identity.valid = !static_cast<bool>(error);
			}
		}

		//+ load asset into cache entry
		{
			try
			{
				created.slot->value = load_asset(created.path);
				created.slot->error.clear();
				++created.slot->revision;
			}
			catch (::std::exception const& failure)
			{
				created.slot->error = failure.what();
			}
		}

		return entries.emplace(::std::move(key), ::std::move(created)).first->second.slot;
	}

	auto poll() -> ::std::size_t
	{
		auto reloaded = ::std::size_t{};

		for (auto&& [key, watched] : entries)
		{
			auto next = file_identity{};

			//+ capture file identity
			{
				auto error = ::std::error_code{};
				if (::std::filesystem::exists(watched.path, error) && !error)
				{
					next.last_write_time = ::std::filesystem::last_write_time(watched.path, error);
					next.file_size = ::std::filesystem::file_size(watched.path, error);
					next.valid = !static_cast<bool>(error);
				}
			}

			if (next.valid == watched.last_identity.valid
				&& next.file_size == watched.last_identity.file_size
				&& next.last_write_time == watched.last_identity.last_write_time)
			{
				continue;
			}

			watched.last_identity = next;

			//+ load asset into cache entry
			{
				try
				{
					watched.slot->value = load_asset(watched.path);
					watched.slot->error.clear();
					++watched.slot->revision;
					++reloaded;
				}
				catch (::std::exception const& failure)
				{
					watched.slot->error = failure.what();
				}
			}
		}

		return reloaded;
	}

	struct file_identity
	{
		::std::filesystem::file_time_type last_write_time = {};
		::std::uintmax_t file_size = 0;
		bool valid = false;
	};

	struct entry
	{
		::std::shared_ptr<asset<asset_type>> slot;
		::std::filesystem::path path;
		file_identity last_identity;
	};

	load_function load_asset;
	::std::unordered_map<::std::string, entry> entries;
};
}
