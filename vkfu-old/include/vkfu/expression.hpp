#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "./concepts/expression.hpp"

namespace vkfu
{
template<class T>
struct object_traits;

template<class T>
struct parameter_traits;

template<class T>
concept parameter = requires
{
	typename parameter_traits<::std::remove_cvref_t<T>>::object_type;
	typename parameter_traits<::std::remove_cvref_t<T>>::native_type;
	requires vulkan_object<typename parameter_traits<::std::remove_cvref_t<T>>::object_type>;
	{ parameter_traits<::std::remove_cvref_t<T>>::category } -> ::std::convertible_to<expression_category>;
};

template<class Parent, class Child>
struct attachment_rule
{
	inline static constexpr bool valid = false;
	inline static constexpr bool allow_duplicate = false;
};

template<expression_category Category, class Param, class... Features>
class node_expression;

template<class Payload>
class feature_expression;

template<expression_category Category, class Param, class... Children>
class evaluated_node;

template<class Evaluated>
class evaluated_reference;

template<class T>
	requires parameter<T>
struct expression_traits<T>
{
	using result_type = typename parameter_traits<T>::object_type;
	inline static constexpr expression_category category = parameter_traits<T>::category;
};

template<expression_category Category, class Param, class... Features>
struct expression_traits<node_expression<Category, Param, Features...>>
{
	using result_type = typename parameter_traits<Param>::object_type;
	inline static constexpr expression_category category = Category;
};

template<class Payload>
struct expression_traits<feature_expression<Payload>>
{
	using result_type = typename expression_traits<Payload>::result_type;
	inline static constexpr expression_category category = expression_category::feature;
};

template<expression_category Category, class Param, class... Children>
struct expression_traits<evaluated_node<Category, Param, Children...>>
{
	using result_type = typename parameter_traits<Param>::object_type;
	inline static constexpr expression_category category = Category;
};

template<class Evaluated>
struct expression_traits<evaluated_reference<Evaluated>>
{
	using evaluated_type = ::std::remove_cv_t<Evaluated>;
	using result_type = typename expression_traits<evaluated_type>::result_type;
	inline static constexpr expression_category category = expression_traits<evaluated_type>::category;
};

template<class Object>
inline constexpr bool participates_in_pnext_v = object_traits<Object>::participates_in_pnext;

template<class Parent, class Child>
inline constexpr bool can_attach_v = attachment_rule<Parent, Child>::valid;

template<expression_category Category, class Param, class... Features>
class node_expression
{
public:
	using parameter_type = Param;
	using features_type = ::std::tuple<Features...>;

	constexpr explicit node_expression(Param parameter)
		: parameter_(::std::move(parameter))
	{}

	constexpr node_expression(Param parameter, features_type features)
		: parameter_(::std::move(parameter))
		, features_(::std::move(features))
	{}

	constexpr auto parameter() & noexcept -> Param& { return parameter_; }
	constexpr auto parameter() const& noexcept -> Param const& { return parameter_; }
	constexpr auto parameter() && noexcept -> Param&& { return ::std::move(parameter_); }

	constexpr auto features() & noexcept -> features_type& { return features_; }
	constexpr auto features() const& noexcept -> features_type const& { return features_; }
	constexpr auto features() && noexcept -> features_type&& { return ::std::move(features_); }

private:
	Param parameter_;
	features_type features_{};
};

template<class Payload>
class feature_expression
{
public:
	using payload_type = Payload;

	constexpr explicit feature_expression(Payload payload)
		: payload_(::std::move(payload))
	{}

	constexpr auto payload() & noexcept -> Payload& { return payload_; }
	constexpr auto payload() const& noexcept -> Payload const& { return payload_; }
	constexpr auto payload() && noexcept -> Payload&& { return ::std::move(payload_); }

private:
	Payload payload_;
};

template<class Evaluated>
class evaluated_reference
{
public:
	using evaluated_type = Evaluated;

	constexpr explicit evaluated_reference(Evaluated& value) noexcept
		: value_(::std::addressof(value))
	{}

	constexpr auto get() const noexcept -> Evaluated& { return *value_; }

	auto native_address() const noexcept -> void*
	{
		// Vulkan declares several input feature-chain pNext members as void*.
		// The parent only stores this address and never mutates a borrowed chain.
		return const_cast<void*>(
			static_cast<void const*>(value_->native_address()));
	}

private:
	Evaluated* value_;
};

namespace detail
{
template<class T>
inline constexpr bool is_node_expression_v = false;

template<expression_category Category, class Param, class... Features>
inline constexpr bool is_node_expression_v<node_expression<Category, Param, Features...>> = true;

template<class T>
inline constexpr bool is_feature_expression_v = false;

template<class Payload>
inline constexpr bool is_feature_expression_v<feature_expression<Payload>> = true;

template<class T>
inline constexpr bool is_evaluated_node_v = false;

template<expression_category Category, class Param, class... Children>
inline constexpr bool is_evaluated_node_v<evaluated_node<Category, Param, Children...>> = true;

template<class T>
inline constexpr bool is_evaluated_reference_v = false;

template<class Evaluated>
inline constexpr bool is_evaluated_reference_v<evaluated_reference<Evaluated>> = true;

template<class T>
inline constexpr bool is_supported_payload_v =
	parameter<T> || is_node_expression_v<::std::remove_cvref_t<T>> ||
	is_evaluated_node_v<::std::remove_cvref_t<T>> ||
	is_evaluated_reference_v<::std::remove_cvref_t<T>>;

template<class T>
struct terminal_borrowed_chain : ::std::false_type
{};

template<class Evaluated>
struct terminal_borrowed_chain<evaluated_reference<Evaluated>>
	: ::std::bool_constant<participates_in_pnext_v<expression_result_t<Evaluated>>>
{};

template<class Payload>
struct terminal_borrowed_chain<feature_expression<Payload>>
	: terminal_borrowed_chain<Payload>
{};

template<class... Children>
consteval bool children_end_in_borrowed_chain()
{
	bool result = false;
	((participates_in_pnext_v<expression_result_t<Children>>
		? result = terminal_borrowed_chain<::std::remove_cvref_t<Children>>::value
		: result), ...);
	return result;
}

template<expression_category Category, class Param, class... Features>
struct terminal_borrowed_chain<node_expression<Category, Param, Features...>>
	: ::std::bool_constant<children_end_in_borrowed_chain<Features...>()>
{};

template<expression_category Category, class Param, class... Children>
struct terminal_borrowed_chain<evaluated_node<Category, Param, Children...>>
	: ::std::bool_constant<children_end_in_borrowed_chain<Children...>()>
{};

template<class T>
inline constexpr bool terminal_borrowed_chain_v =
	terminal_borrowed_chain<::std::remove_cvref_t<T>>::value;

template<class Object, class... Expressions>
inline constexpr ::std::size_t result_count_v =
	(0u + ... + static_cast<::std::size_t>(
		::std::same_as<Object, expression_result_t<Expressions>>));

template<class ParentObject, class R, class... ExistingFeatures>
consteval bool append_rules()
{
	using rhs_type = ::std::remove_cvref_t<R>;
	if constexpr (!expression<rhs_type>)
	{
		return false;
	}
	else if constexpr (expression_category_v<rhs_type> == expression_category::root)
	{
		return false;
	}
	else
	{
		using child_object = expression_result_t<rhs_type>;
		if constexpr (!attachment_rule<ParentObject, child_object>::valid)
		{
			return false;
		}
		else if constexpr (
			!attachment_rule<ParentObject, child_object>::allow_duplicate &&
			result_count_v<child_object, ExistingFeatures...> != 0)
		{
			return false;
		}
		else if constexpr (
			children_end_in_borrowed_chain<ExistingFeatures...>() &&
			participates_in_pnext_v<child_object>)
		{
			return false;
		}
		else
		{
			return true;
		}
	}
}

template<class L, class R>
struct valid_append : ::std::false_type
{};

template<class L, class R>
	requires parameter<::std::remove_cvref_t<L>>
struct valid_append<L, R>
	: ::std::bool_constant<
		(parameter_traits<::std::remove_cvref_t<L>>::category != expression_category::feature) &&
		append_rules<typename parameter_traits<::std::remove_cvref_t<L>>::object_type, R>()>
{};

template<expression_category Category, class Param, class... Features, class R>
struct valid_append<node_expression<Category, Param, Features...>, R>
	: ::std::bool_constant<
		(Category != expression_category::feature) &&
		append_rules<typename parameter_traits<Param>::object_type, R, Features...>()>
{};

template<class L, class R>
inline constexpr bool valid_append_v =
	valid_append<::std::remove_cvref_t<L>, R>::value;

template<parameter P>
constexpr auto make_natural_node(P&& parameter_value)
{
	using parameter_type = ::std::remove_cvref_t<P>;
	return node_expression<parameter_traits<parameter_type>::category, parameter_type>{
		parameter_type(::std::forward<P>(parameter_value))};
}

template<class T>
constexpr auto normalize_parent(T&& value)
{
	using value_type = ::std::remove_cvref_t<T>;
	if constexpr (parameter<value_type>)
	{
		return make_natural_node(::std::forward<T>(value));
	}
	else
	{
		static_assert(is_node_expression_v<value_type>);
		return value_type(::std::forward<T>(value));
	}
}

template<class T>
constexpr auto make_feature(T&& value)
{
	using value_type = ::std::remove_cvref_t<T>;
	if constexpr (is_feature_expression_v<value_type>)
	{
		return value_type(::std::forward<T>(value));
	}
	else if constexpr (is_evaluated_node_v<value_type> && ::std::is_lvalue_reference_v<T>)
	{
		using referenced_type = ::std::remove_reference_t<T>;
		return feature_expression<evaluated_reference<referenced_type>>{
			evaluated_reference<referenced_type>{value}};
	}
	else if constexpr (parameter<value_type>)
	{
		auto payload = make_natural_node(::std::forward<T>(value));
		return feature_expression<decltype(payload)>{::std::move(payload)};
	}
	else
	{
		static_assert(
			is_node_expression_v<value_type> || is_evaluated_node_v<value_type> ||
			is_evaluated_reference_v<value_type>);
		return feature_expression<value_type>{value_type(::std::forward<T>(value))};
	}
}

template<expression_category Category, class Param, class... Features, class NewFeature>
constexpr auto append(
	node_expression<Category, Param, Features...>&& parent,
	NewFeature&& new_feature)
{
	using new_feature_type = ::std::remove_cvref_t<NewFeature>;
	auto parameter_value = ::std::move(parent).parameter();
	auto features = ::std::tuple_cat(
		::std::move(parent).features(),
		::std::tuple<new_feature_type>{::std::forward<NewFeature>(new_feature)});
	return node_expression<Category, Param, Features..., new_feature_type>{
		::std::move(parameter_value), ::std::move(features)};
}
}

template<parameter P>
constexpr auto root(P&& parameter_value)
	requires (::std::is_constructible_v<::std::remove_cvref_t<P>, P&&>)
{
	using parameter_type = ::std::remove_cvref_t<P>;
	return node_expression<expression_category::root, parameter_type>{
		parameter_type(::std::forward<P>(parameter_value))};
}

template<parameter P>
constexpr auto branch(P&& parameter_value)
	requires (::std::is_constructible_v<::std::remove_cvref_t<P>, P&&>)
{
	using parameter_type = ::std::remove_cvref_t<P>;
	return node_expression<expression_category::branch, parameter_type>{
		parameter_type(::std::forward<P>(parameter_value))};
}

template<class T>
constexpr auto feature(T&& value)
	requires (
		expression<::std::remove_cvref_t<T>> &&
		expression_category_v<::std::remove_cvref_t<T>> != expression_category::root)
{
	return detail::make_feature(::std::forward<T>(value));
}

template<class L, class R>
constexpr auto operator|(L&& left, R&& right)
	requires detail::valid_append_v<L, R&&>
{
	auto parent = detail::normalize_parent(::std::forward<L>(left));
	auto child = detail::make_feature(::std::forward<R>(right));
	return detail::append(::std::move(parent), ::std::move(child));
}

namespace detail
{
template<class E>
struct valid_expression_graph_impl : ::std::false_type
{};

template<class P>
	requires parameter<P>
struct valid_expression_graph_impl<P> : ::std::true_type
{};

template<class Evaluated>
struct valid_expression_graph_impl<evaluated_reference<Evaluated>> : ::std::true_type
{};

template<expression_category Category, class Param, class... Children>
struct valid_expression_graph_impl<evaluated_node<Category, Param, Children...>> : ::std::true_type
{};

template<class Payload>
struct valid_expression_graph_impl<feature_expression<Payload>>
	: valid_expression_graph_impl<Payload>
{};

template<class ParentObject, class... Features>
consteval bool direct_features_are_valid()
{
	return ((
		attachment_rule<ParentObject, expression_result_t<Features>>::valid &&
		(attachment_rule<ParentObject, expression_result_t<Features>>::allow_duplicate ||
			result_count_v<expression_result_t<Features>, Features...> == 1) &&
		valid_expression_graph_impl<Features>::value) && ...);
}

template<class... Features>
consteval bool borrowed_layout_is_valid()
{
	bool borrowed_tail_seen = false;
	bool valid = true;
	((participates_in_pnext_v<expression_result_t<Features>>
		? (valid = valid && !borrowed_tail_seen,
			borrowed_tail_seen = terminal_borrowed_chain_v<Features>)
		: false), ...);
	return valid;
}

template<expression_category Category, class Param, class... Features>
struct valid_expression_graph_impl<node_expression<Category, Param, Features...>>
	: ::std::bool_constant<
		direct_features_are_valid<typename parameter_traits<Param>::object_type, Features...>() &&
		borrowed_layout_is_valid<Features...>()>
{};
}

template<class E>
inline constexpr bool valid_expression_graph_v =
	detail::valid_expression_graph_impl<::std::remove_cvref_t<E>>::value;

namespace detail
{
struct no_native
{};
}

template<expression_category Category, class Param, class... Children>
class evaluated_node
{
public:
	using parameter_type = Param;
	using native_type = typename parameter_traits<Param>::native_type;
	using children_type = ::std::tuple<Children...>;
	using object_type = typename parameter_traits<Param>::object_type;

	evaluated_node(Param parameter, children_type children)
		: parameter_(::std::move(parameter))
		, native_(parameter_traits<Param>::make_native(parameter_))
		, children_(::std::move(children))
	{
		relink();
	}

	evaluated_node(evaluated_node const& other)
		: parameter_(other.parameter_)
		, native_(parameter_traits<Param>::make_native(parameter_))
		, children_(other.children_)
	{
		relink();
	}

	evaluated_node(evaluated_node&& other)
		noexcept(
			::std::is_nothrow_move_constructible_v<Param> &&
			::std::is_nothrow_move_constructible_v<children_type> &&
			noexcept(parameter_traits<Param>::make_native(::std::declval<Param const&>())))
		: parameter_(::std::move(other.parameter_))
		, native_(parameter_traits<Param>::make_native(parameter_))
		, children_(::std::move(other.children_))
	{
		relink();
	}

	auto operator=(evaluated_node const& other) -> evaluated_node&
	{
		if (this != ::std::addressof(other))
		{
			parameter_ = other.parameter_;
			children_ = other.children_;
			native_ = parameter_traits<Param>::make_native(parameter_);
			relink();
		}
		return *this;
	}

	auto operator=(evaluated_node&& other)
		noexcept(
			::std::is_nothrow_move_assignable_v<Param> &&
			::std::is_nothrow_move_assignable_v<children_type> &&
			noexcept(parameter_traits<Param>::make_native(::std::declval<Param const&>())))
		-> evaluated_node&
	{
		if (this != ::std::addressof(other))
		{
			parameter_ = ::std::move(other.parameter_);
			children_ = ::std::move(other.children_);
			native_ = parameter_traits<Param>::make_native(parameter_);
			relink();
		}
		return *this;
	}

	constexpr auto parameter() & noexcept -> Param& { return parameter_; }
	constexpr auto parameter() const& noexcept -> Param const& { return parameter_; }

	constexpr auto children() & noexcept -> children_type& { return children_; }
	constexpr auto children() const& noexcept -> children_type const& { return children_; }

	constexpr auto native() & noexcept -> native_type&
		requires participates_in_pnext_v<object_type>
	{
		return native_;
	}

	constexpr auto native() const& noexcept -> native_type const&
		requires participates_in_pnext_v<object_type>
	{
		return native_;
	}

	auto native_address() noexcept -> void*
		requires participates_in_pnext_v<object_type>
	{
		return static_cast<void*>(::std::addressof(native_));
	}

	auto native_address() const noexcept -> void const*
		requires participates_in_pnext_v<object_type>
	{
		return static_cast<void const*>(::std::addressof(native_));
	}

	void relink() noexcept
	{
		(void)relink_with_successor(nullptr);
	}

private:
	template<expression_category, class, class...>
	friend class evaluated_node;

	template<class Child>
	static auto link_child(Child& child, void* successor) noexcept -> void*
	{
		using child_type = ::std::remove_cvref_t<Child>;
		using child_object = expression_result_t<child_type>;
		if constexpr (!participates_in_pnext_v<child_object>)
		{
			return successor;
		}
		else if constexpr (detail::is_evaluated_reference_v<child_type>)
		{
			assert(successor == nullptr &&
				"a borrowed evaluated pNext chain must be the final chain child");
			return child.native_address();
		}
		else
		{
			if constexpr (detail::terminal_borrowed_chain_v<child_type>)
			{
				assert(successor == nullptr &&
					"an owned subtree ending in a borrowed chain must be final");
			}
			return child.relink_with_successor(successor);
		}
	}

	template<::std::size_t... Index>
	auto link_children_reverse(void* successor, ::std::index_sequence<Index...>) noexcept -> void*
	{
		void* next = successor;
		((next = link_child(
			::std::get<sizeof...(Children) - 1u - Index>(children_), next)), ...);
		return next;
	}

	auto relink_with_successor(void* successor) noexcept -> void*
	{
		if constexpr (!participates_in_pnext_v<object_type>)
		{
			return successor;
		}
		else
		{
			void* child_head = link_children_reverse(
				successor, ::std::index_sequence_for<Children...>{});
			parameter_traits<Param>::set_pnext(native_, child_head);
			return native_address();
		}
	}

	Param parameter_;
	native_type native_;
	children_type children_;
};

namespace detail
{
template<class Child>
auto materialize_child(Child&& child);

template<class Tuple, ::std::size_t... Index>
auto materialize_feature_tuple(Tuple&& features, ::std::index_sequence<Index...>)
{
	return ::std::tuple{
		materialize_child(
			::std::get<Index>(::std::forward<Tuple>(features)).payload())...};
}

template<expression_category Category, class ParamArg, class FeaturesTuple>
auto materialize_node_parts(ParamArg&& parameter_value, FeaturesTuple&& features)
{
	using parameter_type = ::std::remove_cvref_t<ParamArg>;
	parameter_type stored_parameter(::std::forward<ParamArg>(parameter_value));
	constexpr auto feature_count =
		::std::tuple_size_v<::std::remove_cvref_t<FeaturesTuple>>;
	auto children = materialize_feature_tuple(
		::std::forward<FeaturesTuple>(features),
		::std::make_index_sequence<feature_count>{});

	return [&]<class... Child>(::std::tuple<Child...>&& child_values)
	{
		return evaluated_node<Category, parameter_type, Child...>{
			::std::move(stored_parameter), ::std::move(child_values)};
	}(::std::move(children));
}

template<expression_category Category, class Param, class... Features>
auto materialize_node(node_expression<Category, Param, Features...>& expression_value)
{
	return materialize_node_parts<Category>(
		expression_value.parameter(), expression_value.features());
}

template<expression_category Category, class Param, class... Features>
auto materialize_node(node_expression<Category, Param, Features...> const& expression_value)
{
	return materialize_node_parts<Category>(
		expression_value.parameter(), expression_value.features());
}

template<expression_category Category, class Param, class... Features>
auto materialize_node(node_expression<Category, Param, Features...>&& expression_value)
{
	auto stored_parameter = ::std::move(expression_value).parameter();
	return materialize_node_parts<Category>(
		::std::move(stored_parameter), ::std::move(expression_value).features());
}

template<class Child>
auto materialize_child(Child&& child)
{
	using child_type = ::std::remove_cvref_t<Child>;
	if constexpr (is_node_expression_v<child_type>)
	{
		return materialize_node(::std::forward<Child>(child));
	}
	else
	{
		static_assert(
			is_evaluated_node_v<child_type> || is_evaluated_reference_v<child_type>);
		return child_type(::std::forward<Child>(child));
	}
}
}

template<parameter P>
auto evaluate(P&& parameter_value)
	requires (
		parameter_traits<::std::remove_cvref_t<P>>::category != expression_category::feature)
{
	auto expression_value = detail::make_natural_node(::std::forward<P>(parameter_value));
	return detail::materialize_node(::std::move(expression_value));
}

template<expression_category Category, class Param, class... Features>
auto evaluate(node_expression<Category, Param, Features...>& expression_value)
	requires (Category != expression_category::feature)
{
	static_assert(valid_expression_graph_v<decltype(expression_value)>,
		"vkfu: invalid expression graph");
	return detail::materialize_node(expression_value);
}

template<expression_category Category, class Param, class... Features>
auto evaluate(node_expression<Category, Param, Features...> const& expression_value)
	requires (Category != expression_category::feature)
{
	static_assert(valid_expression_graph_v<decltype(expression_value)>,
		"vkfu: invalid expression graph");
	return detail::materialize_node(expression_value);
}

template<expression_category Category, class Param, class... Features>
auto evaluate(node_expression<Category, Param, Features...>&& expression_value)
	requires (Category != expression_category::feature)
{
	static_assert(valid_expression_graph_v<decltype(expression_value)>,
		"vkfu: invalid expression graph");
	return detail::materialize_node(::std::move(expression_value));
}

template<expression_category Category, class Param, class... Children>
constexpr auto evaluate(evaluated_node<Category, Param, Children...>& value) noexcept
	-> evaluated_node<Category, Param, Children...>&
	requires (Category != expression_category::feature)
{
	return value;
}

template<expression_category Category, class Param, class... Children>
constexpr auto evaluate(evaluated_node<Category, Param, Children...> const& value) noexcept
	-> evaluated_node<Category, Param, Children...> const&
	requires (Category != expression_category::feature)
{
	return value;
}

template<expression_category Category, class Param, class... Children>
constexpr auto evaluate(evaluated_node<Category, Param, Children...>&& value)
	noexcept(::std::is_nothrow_move_constructible_v<
		evaluated_node<Category, Param, Children...>>)
	-> evaluated_node<Category, Param, Children...>
	requires (Category != expression_category::feature)
{
	return ::std::move(value);
}

template<class E>
using expression_storage_t = ::std::remove_cvref_t<decltype(
	evaluate(::std::declval<::std::remove_cvref_t<E>>()))>;

namespace detail
{
template<class Object, class Storage>
struct direct_child_count : ::std::integral_constant<::std::size_t, 0>
{};

template<class Object, expression_category Category, class Param, class... Children>
struct direct_child_count<Object, evaluated_node<Category, Param, Children...>>
	: ::std::integral_constant<::std::size_t, result_count_v<Object, Children...>>
{};

template<class Object, expression_category Category, class Param, class... Features>
struct direct_child_count<Object, node_expression<Category, Param, Features...>>
	: ::std::integral_constant<::std::size_t, result_count_v<Object, Features...>>
{};

template<class Object, class First, class... Rest>
consteval ::std::size_t first_result_index()
{
	if constexpr (::std::same_as<Object, expression_result_t<First>>)
	{
		return 0;
	}
	else
	{
		static_assert(sizeof...(Rest) != 0, "vkfu: requested child is absent");
		return 1u + first_result_index<Object, Rest...>();
	}
}

template<class T>
constexpr decltype(auto) unwrap_child(T& child) noexcept
{
	if constexpr (is_evaluated_reference_v<::std::remove_cvref_t<T>>)
	{
		return child.get();
	}
	else
	{
		return (child);
	}
}

template<class T>
constexpr decltype(auto) unwrap_child(T const& child) noexcept
{
	if constexpr (is_evaluated_reference_v<::std::remove_cvref_t<T>>)
	{
		return static_cast<::std::add_const_t<
			typename ::std::remove_cvref_t<T>::evaluated_type>&>(
			child.get());
	}
	else
	{
		return (child);
	}
}
}

template<class Object, class Storage>
inline constexpr ::std::size_t direct_child_count_v =
	detail::direct_child_count<Object, ::std::remove_cvref_t<Storage>>::value;

template<class Object, expression_category Category, class Param, class... Children>
constexpr decltype(auto) get_child(evaluated_node<Category, Param, Children...>& storage) noexcept
{
	static_assert(detail::result_count_v<Object, Children...> == 1,
		"vkfu: get_child requires exactly one matching direct child");
	constexpr ::std::size_t index = detail::first_result_index<Object, Children...>();
	return detail::unwrap_child(::std::get<index>(storage.children()));
}

template<class Object, expression_category Category, class Param, class... Children>
constexpr decltype(auto) get_child(
	evaluated_node<Category, Param, Children...> const& storage) noexcept
{
	static_assert(detail::result_count_v<Object, Children...> == 1,
		"vkfu: get_child requires exactly one matching direct child");
	constexpr ::std::size_t index = detail::first_result_index<Object, Children...>();
	auto const& child = ::std::get<index>(storage.children());
	if constexpr (detail::is_evaluated_reference_v<
		::std::remove_cvref_t<decltype(child)>>)
	{
		return static_cast<::std::add_const_t<
			::std::remove_cvref_t<decltype(child.get())>>&>(
			child.get());
	}
	else
	{
		return (child);
	}
}
}
