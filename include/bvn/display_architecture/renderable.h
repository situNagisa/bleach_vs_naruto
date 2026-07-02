#pragma once

#include <utility>

#include <stdexec/execution.hpp>

namespace bvn::display_architecture
{
	namespace render_customization
	{
		void render();

		enum class render_choose
		{
			member,
			adl,
			none,
		};

		struct render_choice_result
		{
			render_choose choice;
			bool nothrow;
		};

		template<class T, class Renderer>
		constexpr render_choice_result render_choice(T&& t, Renderer&& renderer) noexcept
		{
			if constexpr (requires { ::std::forward<T>(t).render(::std::forward<Renderer>(renderer)); })
			{
				return { render_choose::member, noexcept(::std::forward<T>(t).render(::std::forward<Renderer>(renderer))) };
			}
			else if constexpr (requires { render(::std::forward<T>(t), ::std::forward<Renderer>(renderer)); })
			{
				return { render_choose::adl, noexcept(render(::std::forward<T>(t), ::std::forward<Renderer>(renderer))) };
			}
			else
			{
				return { render_choose::none, true };
			}
		}

		struct render_t
		{
			template <class T, class Renderer>
			constexpr
#if __cpp_static_call_operator
				static
#endif 
				decltype(auto) operator()(T&& t, Renderer&& renderer)
#if !__cpp_static_call_operator
				const
#endif
				noexcept(render_customization::render_choice(::std::forward<T>(t), ::std::forward<Renderer>(renderer)).nothrow)
				requires (render_customization::render_choice(::std::forward<T>(t), ::std::forward<Renderer>(renderer)).choice != render_choose::none)
			{
				constexpr render_choose choice = render_customization::render_choice(::std::forward<T>(t), ::std::forward<Renderer>(renderer)).choice;
				if constexpr (choice == render_choose::member)
					return ::std::forward<T>(t).render(::std::forward<Renderer>(renderer));
				else if constexpr (choice == render_choose::adl)
					return render(::std::forward<T>(t), ::std::forward<Renderer>(renderer));
				else
					;
			}
		};
	}

	inline constexpr render_customization::render_t render{};

	template <class T, class Renderer>
	concept renderable = requires (T && t, Renderer && renderer)
	{
		{ display_architecture::render(::std::forward<T>(t), ::std::forward<Renderer>(renderer)) } -> ::stdexec::sender;
	};
}
