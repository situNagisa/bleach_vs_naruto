#pragma once

#include <utility>
#include <version>

namespace bvn::display_architecture
{
	// Customization point: render(thing, renderer) dispatches to thing.render(renderer).
	// The renderer doubles as the drawing context; the core stays fully generic over it
	// (engine-spec §1 "引擎 = 平台"; display-architecture §1/§3 "一切可视物自己画自己").
	inline constexpr struct render_fn
	{
		template <class T, class Renderer>
			requires requires (T&& t, Renderer&& renderer) { ::std::forward<T>(t).render(::std::forward<Renderer>(renderer)); }
		constexpr
#if __cpp_static_call_operator
		static
#endif
		decltype(auto) operator()(T&& t, Renderer&& renderer)
#if !__cpp_static_call_operator
		const
#endif
			noexcept(noexcept(::std::forward<T>(t).render(::std::forward<Renderer>(renderer))))
		{
			return ::std::forward<T>(t).render(::std::forward<Renderer>(renderer));
		}
	} render{};

	// A renderable knows how to draw itself when handed a renderer / drawing context.
	// How it draws — batched, SoA, one quad or ten thousand — is entirely its own business.
	template <class T, class Renderer>
	concept renderable = requires (T&& t, Renderer&& renderer)
	{
		render(::std::forward<T>(t), ::std::forward<Renderer>(renderer));
	};
}
