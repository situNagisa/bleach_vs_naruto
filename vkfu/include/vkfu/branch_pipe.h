#pragma once

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

#include "./expression.h"
#include "./storage.h"
#include "./vulkan_object.h"

namespace vkfu
{
template<class Tag, class Features>
inline constexpr auto _contains_vulkan_tag_v = false;

template<class Tag, class... Features>
inline constexpr auto _contains_vulkan_tag_v<Tag, ::std::tuple<Features...>> = (::std::same_as<Tag, expression_vulkan_tag_t<Features>> || ...);

template<branch_expression Branch, expression... Features>
struct branch_pipe_expression
{
	using self_type = branch_pipe_expression;
	using branch_type = Branch;
	using features_type = ::std::tuple<Features...>;
	using vulkan_tag_type = expression_vulkan_tag_t<branch_type>;

	constexpr explicit branch_pipe_expression(branch_type branch, Features... features)
		noexcept(::std::is_nothrow_constructible_v<branch_type, branch_type&&> && ::std::is_nothrow_constructible_v<features_type, Features&&...>)
		: _branch(::std::forward<branch_type>(branch)), _features(::std::forward<Features>(features)...)
	{}

	constexpr auto evaluate() noexcept(noexcept(_evaluate(::std::index_sequence_for<Features...>{})))
	{
		return self_type::_evaluate(::std::index_sequence_for<Features...>{});
	}

	constexpr auto append(expression auto&&... features) noexcept(noexcept(_append(::std::index_sequence_for<Features...>{}, ::std::forward<decltype(features)>(features)...)))
	{
		return self_type::_append(::std::index_sequence_for<Features...>{}, ::std::forward<decltype(features)>(features)...);
	}

	template<::std::size_t... Indices>
	constexpr auto _evaluate(::std::index_sequence<Indices...>)
		noexcept(noexcept(basic_storage{vkfu::evaluate(::std::forward<branch_type>(_branch)), vkfu::evaluate(::std::forward<Features>(::std::get<Indices>(_features)))...}))
	{
		return basic_storage{vkfu::evaluate(::std::forward<branch_type>(_branch)), vkfu::evaluate(::std::forward<Features>(::std::get<Indices>(_features)))...};
	}

	template<::std::size_t... Indices>
	constexpr auto _append(::std::index_sequence<Indices...>, expression auto&&... features)
		noexcept(noexcept(vkfu::branch_pipe_expression{::std::forward<branch_type>(_branch), ::std::forward<Features>(::std::get<Indices>(_features))..., ::std::forward<decltype(features)>(features)...}))
	{
		return vkfu::branch_pipe_expression{::std::forward<branch_type>(_branch), ::std::forward<Features>(::std::get<Indices>(_features))..., ::std::forward<decltype(features)>(features)...};
	}

	branch_type _branch;
	features_type _features;
};

template<branch_expression Branch, expression... Features>
branch_pipe_expression(Branch&&, Features&&...) -> branch_pipe_expression<Branch, Features...>;

template<branch_expression Branch, expression... Features>
void _match_branch_pipe_expression(branch_pipe_expression<Branch, Features...> const&);

template<class T>
concept _derived_from_branch_pipe_expression = requires(T const& expression)
{
	_match_branch_pipe_expression(expression);
};

namespace branch_pipe_customization
{
enum class choice
{
	none,
	append,
	construct,
};

struct choice_result
{
	choice selected;
	bool nothrow;
};

// A feature that is not marked allow_duplicate may appear at most once per
// branch. The check is local to this level: a nested branch carries its own
// feature list, so it is not flattened into its parent's.
template<class Branch, class Feature>
consteval auto duplicates_feature() noexcept -> bool
{
	using tag_type = expression_vulkan_tag_t<Feature>;
	if constexpr (duplicatable_vulkan_object<tag_type>)
	{
		return false;
	}
	else
	{
		return _contains_vulkan_tag_v<tag_type, typename ::std::remove_cvref_t<Branch>::features_type>;
	}
}

template<class Branch, class Feature>
consteval auto choose_branch_pipe() noexcept -> choice_result
{
	if constexpr (!vulkan_object_compatible_with<expression_vulkan_tag_t<Branch>, expression_vulkan_tag_t<Feature>>)
	{
		return { choice::none, true };
	}
	else if constexpr (_derived_from_branch_pipe_expression<Branch>)
	{
		if constexpr (duplicates_feature<Branch, Feature>())
		{
			return { choice::none, true };
		}
		else
		{
			return {choice::append, noexcept(::std::declval<Branch>().append(::std::declval<Feature>()))};
		}
	}
	else
	{
		return {choice::construct, noexcept(branch_pipe_expression{::std::declval<Branch>(), ::std::declval<Feature>()})};
	}
}
}

constexpr auto operator|(branch_expression auto&& branch, expression auto&& feature) noexcept(branch_pipe_customization::choose_branch_pipe<decltype(branch), decltype(feature)>().nothrow)
	requires (branch_pipe_customization::choose_branch_pipe<decltype(branch), decltype(feature)>().selected != branch_pipe_customization::choice::none)
{
	constexpr auto selected = branch_pipe_customization::choose_branch_pipe<decltype(branch), decltype(feature)>().selected;
	if constexpr (selected == branch_pipe_customization::choice::append)
	{
		return ::std::forward<decltype(branch)>(branch).append(::std::forward<decltype(feature)>(feature));
	}
	else
	{
		return branch_pipe_expression{::std::forward<decltype(branch)>(branch), ::std::forward<decltype(feature)>(feature)};
	}
}
}
