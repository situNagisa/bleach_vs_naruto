#pragma once

#include "./environment.h"

NAGISA_BUILD_LIB_DETAIL_BEGIN

struct loader_t
{
	using self_type = loader_t;

	struct module
	{
		using self_type = module;

		constexpr module(
			loader_t& loader
			, ::nagisa::symbol_loader::loaders::native::dll_observer::handle_type handle
			, ::std::span<void const* const> table) noexcept
			: _loader(::std::addressof(loader))
			, _handle(handle)
			, _table(table)
		{}

		~module()
		{
			if (!_loader)
				return;
			auto&& [count, data] = _loader->_data.at(_handle);
			if (--count == 0)
			{
				_loader->_data.erase(_handle);
			}
		}

		constexpr module(self_type&& other) noexcept
			: _loader(other._loader)
			, _handle(::std::exchange(other._handle, {}))
			, _table(::std::exchange(other._table, {}))
		{}
		constexpr self_type& operator=(self_type&& other) noexcept
		{
			::std::swap(_loader, other._loader);
			::std::swap(_handle, other._handle);
			::std::swap(_table, other._table);
			return *this;
		}

		constexpr module(self_type const&) = delete;
		constexpr self_type& operator=(self_type const&) = delete;

		constexpr auto table() const noexcept { return _table; }

		loader_t* _loader;
		::nagisa::symbol_loader::loaders::native::dll_observer::handle_type _handle;
		::std::span<void const* const> _table;
	};

	module load(char const* name, ::std::ranges::input_range auto const& symbols)
		requires ::std::convertible_to<::std::ranges::range_reference_t<decltype(symbols)>, char const* const>
	{
		// assert(!self_type::contains(name));
		auto handle = ::nagisa::symbol_loader::loaders::native::load_dll(name);
		if (_data.contains(handle))
		{
			auto&& [count, data] = _data.at(handle);
			++count;
			if constexpr(::std::ranges::sized_range<decltype(symbols)>)
			{
				// assert(::std::ranges::size(symbols) == ::std::ranges::size(data));
			}
			return { *this, handle, data };
		}
		auto [it, success] = _data.try_emplace(
			handle
			, 1u
			, symbols | std::views::transform(
				[handle](char const* symbol_name)
				{
					return ::nagisa::symbol_loader::loaders::native::get_symbol_address(handle, symbol_name);
				}
			) | std::ranges::to<::std::vector<void const*>>()
		);
		if (!success)
		{
			// throw
		}
		return { *this, handle, it->second.second };
	}

	::std::unordered_map<
		::nagisa::symbol_loader::loaders::native::dll_observer::handle_type
		, ::std::pair<::std::atomic_uint_least32_t, ::std::vector<void const*>>
	> _data{};
};

NAGISA_BUILD_LIB_DETAIL_END