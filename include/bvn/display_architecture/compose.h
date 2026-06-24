#pragma once

#include <ranges>
#include <utility>

#include <bvn/display_architecture/renderable.h>

namespace bvn::display_architecture
{
	// Composition mechanism: collect heterogeneous renderables, draw each in submission order.
	// No storage, no type erasure — every call is monomorphic (display-architecture §3/§9
	// "先提交序，不够再加"; sort keys / type-erased frame lists are a later concern).
	template <class Renderer, class... Renderables>
		requires (renderable<Renderables, Renderer&> and ...)
	constexpr void render_all(Renderer& renderer, Renderables&&... renderables)
	{
		(render(::std::forward<Renderables>(renderables), renderer), ...);
	}

	// Convenience for a homogeneous range of renderables (e.g. one sprite per unit).
	template <class Renderer, ::std::ranges::input_range Range>
		requires renderable<::std::ranges::range_reference_t<Range>, Renderer&>
	constexpr void render_each(Renderer& renderer, Range&& range)
	{
		for (auto&& item : ::std::forward<Range>(range))
		{
			render(item, renderer);
		}
	}
}
