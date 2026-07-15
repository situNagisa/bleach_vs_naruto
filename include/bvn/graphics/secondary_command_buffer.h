#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>
#include <vkkl/command_buffer.h>
#include <vkkl/command_pool.h>

#include <bvn/graphics/environment.h>

NAGISA_BUILD_LIB_DETAIL_BEGIN

template <class T>
struct free_list
{
	static constexpr auto _empty = ::std::numeric_limits<::std::size_t>::max();

	struct entry
	{
		T _value;
		::std::size_t _next_free = _empty;
		bool _acquired = true;
	};

	::std::vector<entry> _entries;
	::std::size_t _first_free = _empty;

	template <class Factory>
	constexpr auto acquire(Factory&& factory) -> ::std::pair<::std::size_t, T&>
	{
		if (_first_free != _empty)
		{
			auto const index = _first_free;
			auto&& current = _entries[index];
			assert(!current._acquired);
			_first_free = ::std::exchange(current._next_free, _empty);
			current._acquired = true;
			return {index, current._value};
		}

		auto const index = _entries.size();
		_entries.emplace_back(entry
		{
			._value = ::std::invoke(::std::forward<Factory>(factory)),
		});
		return {index, _entries.back()._value};
	}

	constexpr auto release(::std::size_t index) noexcept -> void
	{
		assert(index < _entries.size());
		auto&& current = _entries[index];
		assert(current._acquired);
		current._acquired = false;
		current._next_free = ::std::exchange(_first_free, index);
	}
};

NAGISA_BUILD_LIB_DETAIL_END

NAGISA_BUILD_LIB_BEGIN

/// Owns one render task's secondary command pool and all buffers allocated from it.
struct secondary_command_pool
{
	struct command_buffer
	{
		secondary_command_pool* _pool = nullptr;
		::std::size_t _index = 0;
		::VkCommandBuffer _handle = VK_NULL_HANDLE;

		constexpr command_buffer() noexcept = default;

		constexpr command_buffer(secondary_command_pool& pool, ::std::size_t index, ::VkCommandBuffer handle) noexcept
			: _pool(&pool)
			, _index(index)
			, _handle(handle)
		{}

		command_buffer(command_buffer const&) = delete;
		auto operator=(command_buffer const&) -> command_buffer& = delete;

		constexpr command_buffer(command_buffer&& other) noexcept
			: _pool(::std::exchange(other._pool, nullptr))
			, _index(::std::exchange(other._index, 0))
			, _handle(::std::exchange(other._handle, VK_NULL_HANDLE))
		{}

		auto operator=(command_buffer&&) -> command_buffer& = delete;

		~command_buffer() noexcept;

		[[nodiscard]] constexpr auto get() const noexcept -> ::VkCommandBuffer
		{
			assert(_pool != nullptr);
			assert(_handle != VK_NULL_HANDLE);
			return _handle;
		}

		auto recycle() noexcept -> void;
	};

	explicit secondary_command_pool(::VkDevice device, ::std::uint32_t queue_family);

	secondary_command_pool(secondary_command_pool const&) = delete;
	auto operator=(secondary_command_pool const&) -> secondary_command_pool& = delete;
	secondary_command_pool(secondary_command_pool&&) = delete;
	auto operator=(secondary_command_pool&&) -> secondary_command_pool& = delete;

	[[nodiscard]] auto acquire() -> command_buffer;
	auto recycle(::std::size_t index) noexcept -> void;

	::VkDevice _device = VK_NULL_HANDLE;
	::vkkl::command_pool _pool;
	details::free_list<::vkkl::command_buffer> _commands;
	::std::mutex _mutex;
};

NAGISA_BUILD_LIB_END

#include <nagisa/build_lib/destruct.h>
