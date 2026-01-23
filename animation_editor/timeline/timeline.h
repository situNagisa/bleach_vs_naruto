#pragma once

#include <ranges>
#include <vector>
#include <variant>
#include <tuple>
#include <algorithm>

#include <entt/entt.hpp>

#include <nagisa/dsal/iterator.h>

#include "./frame.h"

struct timeline : ::std::ranges::view_interface<timeline>
{
	struct keyframe
	{
		::std::vector<::entt::handle> displays{};
	};

	struct frame_span
	{
		::std::size_t hold_frame = 1;
	};

	struct _data_type
	{
		keyframe keyframe{};
		frame_span frame_span{};
		::std::size_t prefix_sum{};
	};
	::std::vector<_data_type> _data{};
	auto keyframes() const { return _data | ::std::views::transform(&_data_type::keyframe); }
	auto _prefix_sums() const { return _data | ::std::views::transform(&_data_type::prefix_sum); }

	
	template<bool Const>
	struct iterator_impl : ::nagisa::dsal::iterator_interface<iterator_impl<Const>, ::std::random_access_iterator_tag>
	{
		using keyframe_type = ::std::conditional_t<Const, keyframe const, keyframe>;
		using frame_span_type = ::std::conditional_t<Const, frame_span const, frame_span>;
		using timeline_type = ::std::conditional_t<Const, timeline const, timeline>;

		struct keyframe_observer
		{
			keyframe_type* _data{};

			constexpr auto&& keyframe() const noexcept { return *_data; }
		};
		struct frame_span_observer
		{
			keyframe_type* _keyframe{};
			frame_span_type* _span{};

			constexpr auto&& keyframe() const noexcept { return *_keyframe; }
		};
		using frame = ::std::variant<keyframe_observer, frame_span_observer>;

		timeline_type* _tl{};
		::std::size_t _current_data{};
		::std::size_t _index{};

		constexpr iterator_impl() noexcept = default;
		constexpr iterator_impl(timeline_type& tl, ::std::size_t current_data, ::std::size_t index) noexcept
			: _tl(::std::addressof(tl))
			, _current_data(current_data)
			, _index(index)
		{}
		constexpr iterator_impl(timeline_type& tl, ::std::size_t index) noexcept
			: _tl(::std::addressof(tl))
			, _index(index)
		{
			_update_current_data();
		}
		constexpr void _update_current_data() noexcept
		{
			auto it = ::std::ranges::lower_bound(_tl->_data, _index, {}, &_data_type::prefix_sum);
			_current_data = static_cast<::std::size_t>(::std::ranges::distance(_tl->_data.begin(), it));
		}
		constexpr bool operator==(iterator_impl const& other) const noexcept { return _tl == other._tl && _index == other._index; }
		constexpr auto&& operator*() const noexcept
		{
			auto&& [kf, dur, psum] = _tl->_data[_current_data];
			if (psum == _index)
			{
				return frame{ keyframe_observer{ &kf } };
			}
			return frame{ frame_span_observer{ &kf, &dur } };
		}
		constexpr auto&& operator+=(::std::ptrdiff_t n) noexcept
		{
			_index += n;
			_update_current_data();
			return *this;
		}
		constexpr auto operator-(iterator_impl const& other) const noexcept
		{
			return static_cast<::std::ptrdiff_t>(_index) - static_cast<::std::ptrdiff_t>(other._index);
		}
	};

	using iterator = ::nagisa::dsal::iterator_adaptor<iterator_impl<false>>;
	using const_iterator = ::nagisa::dsal::iterator_adaptor<iterator_impl<true>>;
	constexpr auto size() const noexcept
	{
		if (_data.empty())
			return static_cast<::std::size_t>(0u);
		return _data.back().prefix_sum + _data.back().frame_span.hold_frame;
	}
	constexpr auto begin() noexcept { return iterator{ *this, 0u, 0u }; }
	constexpr auto end() noexcept
	{
		if (_data.empty())
			return iterator{ *this, 0u, 0u };
		return iterator{ *this, _data.size() - 1, _data.back().prefix_sum + _data.back().frame_span.hold_frame, };
	}
	constexpr auto begin() const noexcept { return const_iterator{ *this, 0u, 0u }; }
	constexpr auto end() const noexcept
	{
		if (_data.empty())
			return const_iterator{ *this, 0u, 0u };
		return const_iterator{ *this, _data.size() - 1, _data.back().prefix_sum + _data.back().frame_span.hold_frame, };
	}
	constexpr auto cbegin() const noexcept { return begin(); }
	constexpr auto cend() const noexcept { return end(); }
	

	auto emplace_back(::std::size_t hold_frame = 1)
	{
		auto s = size();
		_data.emplace_back(keyframe{}, frame_span{ hold_frame }, s);
		return ::std::ranges::subrange(begin() + s, end());
	}
};