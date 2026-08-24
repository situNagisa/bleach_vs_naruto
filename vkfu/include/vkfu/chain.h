#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "./branch_pipe.h"
#include "./expression.h"
#include "./vulkan_object.h"

namespace vkfu
{
namespace chain_customization
{
void _vkfu_chain();

enum class choice
{
	adl,
	append,
	construct,
	forward,
	none,
};

struct choice_result
{
	choice selected;
	bool nothrow;
};

/// True when `Feature`'s object appears more than once in the whole pack.
///
/// `operator|` can only ask "is it already there" one step at a time; seeing the
/// whole pack at once means one check instead of a chain of them.
template<class Feature, class... Features>
consteval auto repeats_among() noexcept -> bool
{
	using tag_type = expression_vulkan_tag_t<Feature>;
	if constexpr (duplicatable_vulkan_object<tag_type>)
	{
		return false;
	}
	else
	{
		auto occurrences = ::std::size_t{0};
		((occurrences += ::std::same_as<tag_type, expression_vulkan_tag_t<Features>> ? 1 : 0), ...);
		return occurrences > 1;
	}
}

template<class Branch, class... Features>
consteval auto chainable() noexcept -> bool
{
	if (!(vulkan_object_compatible_with<expression_vulkan_tag_t<Branch>, expression_vulkan_tag_t<Features>> && ...))
	{
		return false;
	}
	if ((repeats_among<Features, Features...>() || ...))
	{
		return false;
	}
	if constexpr (_derived_from_branch_pipe_expression<Branch>)
	{
		// Whatever the branch already carries counts as being at this level too.
		return !(branch_pipe_customization::duplicates_feature<Branch, Features>() || ...);
	}
	else
	{
		return true;
	}
}

template<class Branch, class... Features>
consteval auto choose_chain() noexcept -> choice_result
{
	if constexpr (requires { _vkfu_chain(::std::declval<Branch>(), ::std::declval<Features>()...); })
	{
		return {choice::adl, noexcept(_vkfu_chain(::std::declval<Branch>(), ::std::declval<Features>()...))};
	}
	else if constexpr (sizeof...(Features) == 0)
	{
		// Chaining nothing is the branch itself, the same as folding `|` over an
		// empty pack.
		return {choice::forward, true};
	}
	else if constexpr (!chainable<Branch, Features...>())
	{
		return {choice::none, true};
	}
	else if constexpr (_derived_from_branch_pipe_expression<Branch>)
	{
		return {choice::append, noexcept(::std::declval<Branch>().append(::std::declval<Features>()...))};
	}
	else
	{
		return {choice::construct, noexcept(branch_pipe_expression{::std::declval<Branch>(), ::std::declval<Features>()...})};
	}
}

/// Attach every feature to a branch in one step.
///
/// `chain(branch, a, b, c)` yields exactly the type `branch | a | b | c` does,
/// but builds it directly instead of through two intermediate expression types,
/// and validates the whole set in one pass rather than once per `|`.
///
/// Customizable through ADL as `_vkfu_chain(branch, features...)`.
struct chain_t
{
	constexpr decltype(auto) operator()(branch_expression auto&& branch, expression auto&&... features) const
		noexcept(choose_chain<decltype(branch), decltype(features)...>().nothrow)
		requires (choose_chain<decltype(branch), decltype(features)...>().selected != choice::none)
	{
		constexpr auto selected = choose_chain<decltype(branch), decltype(features)...>().selected;
		if constexpr (selected == choice::adl)
		{
			return _vkfu_chain(::std::forward<decltype(branch)>(branch), ::std::forward<decltype(features)>(features)...);
		}
		else if constexpr (selected == choice::forward)
		{
			return ::std::forward<decltype(branch)>(branch);
		}
		else if constexpr (selected == choice::append)
		{
			return ::std::forward<decltype(branch)>(branch).append(::std::forward<decltype(features)>(features)...);
		}
		else
		{
			return branch_pipe_expression{::std::forward<decltype(branch)>(branch), ::std::forward<decltype(features)>(features)...};
		}
	}
};
}

inline constexpr chain_customization::chain_t chain{};
}
