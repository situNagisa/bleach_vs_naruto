#include <array>
#include <cassert>
#include <stdexcept>

#include "../framework/dependency_graph_v2.h"

namespace bvn::dependency_graph_tests
{
struct member_graph
{
	using key_type = int;

	constexpr bool contains(int) const noexcept { return _built; }
	constexpr void emplace(int key) noexcept { _value = key * 10; _built = true; ++_builds; }
	constexpr int& at(int) noexcept { return _value; }
	constexpr int const& at(int) const noexcept { return _value; }

	bool _built = false;
	int _value = 0;
	int _builds = 0;
};

struct adl_graph
{
	using key_type = int;

	friend constexpr bool contains(adl_graph const& graph, int) noexcept { return graph._built; }
	friend constexpr int& emplace(adl_graph& graph, int key) noexcept
	{
		graph._built = true;
		graph._value = key * 20;
		++graph._builds;
		return graph._value;
	}
	friend constexpr int& at(adl_graph& graph, int) noexcept { return graph._value; }
	friend constexpr int const& at(adl_graph const& graph, int) noexcept { return graph._value; }

	bool _built = false;
	int _value = 0;
	int _builds = 0;
};

struct both_graph : member_graph
{
	friend constexpr bool contains(both_graph const&, int) noexcept(false) { return true; }
	friend constexpr void emplace(both_graph&, int) noexcept(false) { assert(false); }
	friend constexpr int& at(both_graph& graph, int) noexcept(false) { return graph._adl_value; }

	int _adl_value = -1;
};

struct member_resolve_graph : both_graph
{
	constexpr int& resolve(int) noexcept { return _custom_value; }
	friend constexpr int& resolve(member_resolve_graph& graph, int) noexcept(false) { return graph._adl_value; }

	int _custom_value = 123;
};

struct adl_resolve_graph : member_graph
{
	friend constexpr int& resolve(adl_resolve_graph& graph, int) noexcept { return graph._custom_value; }

	int _custom_value = 456;
};

// 适配外部类型：trait 可以覆盖已经存在的嵌套类型。
struct external_graph : adl_graph
{
	using key_type = void;
};

struct no_key_graph {};
struct reference_key_graph : member_graph { using key_type = void; };
}

namespace bvn
{
template<>
struct dependency_graph_key<dependency_graph_tests::external_graph>
{
	using type = int;
};

template<>
struct dependency_graph_key<dependency_graph_tests::external_graph&>
{
	using type = long;
};

template<>
struct dependency_graph_key<dependency_graph_tests::external_graph const&>
{
	using type = unsigned;
};

template<>
struct dependency_graph_key<dependency_graph_tests::external_graph volatile>
{
	using type = short;
};

template<>
struct dependency_graph_key<dependency_graph_tests::reference_key_graph&&>
{
	using type = int;
};

template<>
struct dependency_graph_key<dependency_graph_tests::no_key_graph>
{};

template<>
struct dependency_graph_key<dependency_graph_tests::no_key_graph&&>
{};
}

namespace bvn::dependency_graph_tests
{
struct empty_graph {};
struct missing_operations { using key_type = int; };
struct missing_contains : member_graph { bool contains(int) const = delete; };
struct missing_at : member_graph { int& at(int) = delete; };
struct missing_emplace : member_graph { void emplace(int) = delete; };
struct void_key : member_graph { using key_type = void; };
struct wrong_contains : member_graph { void contains(int) const noexcept {} };
struct explicit_bool
{
	explicit operator bool() const noexcept { return true; }
};
struct explicit_contains : member_graph
{
	explicit_bool contains(int) const noexcept { return {}; }
};
struct wrong_member_with_adl : wrong_contains
{
	friend bool contains(wrong_member_with_adl const&, int) noexcept { return true; }
};
struct lvalue_graph : member_graph
{
	constexpr bool contains(int) & noexcept { return _built; }
};

struct rvalue_graph
{
	using key_type = int;

	constexpr bool contains(int) && noexcept { return _built; }
	constexpr void emplace(int key) && noexcept { _value = key; _built = true; }
	constexpr int&& at(int) && noexcept { return ::std::move(_value); }

	bool _built = false;
	int _value = 0;
};

struct adl_rvalue_graph
{
	using key_type = int;

	friend constexpr bool contains(adl_rvalue_graph&& graph, int) noexcept { return graph._built; }
	friend constexpr void emplace(adl_rvalue_graph&& graph, int key) noexcept { graph._value = key; graph._built = true; }
	friend constexpr int&& at(adl_rvalue_graph&& graph, int) noexcept { return ::std::move(graph._value); }

	bool _built = false;
	int _value = 0;
};

struct immobile_key
{
	constexpr explicit immobile_key(int value) noexcept : _value(value) {}
	immobile_key(immobile_key const&) = delete;
	immobile_key(immobile_key&&) = delete;

	int _value;
};

struct borrowed_key_graph : member_graph
{
	using key_type = immobile_key;

	constexpr bool contains(key_type const&) const noexcept { return _built; }
	constexpr void emplace(key_type const& key) noexcept { member_graph::emplace(key._value); }
	constexpr int& at(key_type const&) noexcept { return _value; }
};

struct void_result_graph
{
	using key_type = int;

	constexpr bool contains(int) const noexcept { return false; }
	constexpr void emplace(int) noexcept {}
	constexpr void at(int) noexcept {}
};

struct throwing_bool
{
	operator bool() const { throw ::std::runtime_error{"contains conversion failed"}; }
	bool operator!() const noexcept = delete;
};

struct throwing_conversion_graph : member_graph
{
	throwing_bool contains(int) const noexcept { return {}; }
};

struct throwing_key
{
	operator int() const { throw ::std::runtime_error{"key conversion failed"}; }
};

struct throwing_contains : member_graph
{
	bool contains(int) const noexcept(false) { return false; }
};
struct throwing_emplace : member_graph
{
	void emplace(int) noexcept(false) {}
};
struct throwing_at : member_graph
{
	int& at(int) noexcept(false) { return _value; }
};
struct throwing_member_resolve : member_graph
{
	int& resolve(int) noexcept(false) { return _value; }
};
struct throwing_adl_resolve : member_graph
{
	friend int& resolve(throwing_adl_resolve& graph, int) noexcept(false) { return graph._value; }
};

template<class Graph>
concept has_key = requires { typename ::bvn::dependency_graph_key_t<Graph>; };

template<class Graph>
concept can_resolve = requires(Graph&& graph) { ::bvn::resolve(::std::forward<Graph>(graph), 1); };

constexpr int refinement(::bvn::dependency_graph auto&&) noexcept { return 1; }

constexpr int refinement(::bvn::resolvable_dependency_graph auto&&) noexcept { return 2; }

static_assert(::bvn::dependency_graph<member_graph>);
static_assert(::bvn::dependency_graph<member_graph const&>);
static_assert(::bvn::resolvable_dependency_graph<member_graph&>);
static_assert(!::bvn::resolvable_dependency_graph<member_graph const&>);
static_assert(::bvn::resolvable_dependency_graph<adl_graph&>);
static_assert(::bvn::dependency_graph<adl_graph const&>);
static_assert(!::bvn::resolvable_dependency_graph<adl_graph const&>);
static_assert(::bvn::resolvable_dependency_graph<external_graph&>);
static_assert(::std::same_as<::bvn::dependency_graph_key_t<external_graph>, int>);
static_assert(::std::same_as<::bvn::dependency_graph_key_t<external_graph&>, long>);
static_assert(::std::same_as<::bvn::dependency_graph_key_t<external_graph const&>, unsigned>);
static_assert(::std::same_as<::bvn::dependency_graph_key_t<external_graph volatile>, short>);
static_assert(::std::same_as<::bvn::dependency_graph_key_t<external_graph const>, void>);
static_assert(::std::same_as<::bvn::dependency_graph_key<member_graph const volatile&>::type, int>);
static_assert(::std::same_as<::bvn::dependency_graph_key_t<member_graph volatile&&>, int>);
static_assert(!::bvn::dependency_graph<reference_key_graph>);
static_assert(::bvn::resolvable_dependency_graph<reference_key_graph&&>);
static_assert(!::bvn::dependency_graph<member_graph volatile&>);
// 主模板要求 key_type；无 type 的显式特化仍可通过 alias 的约束检测。
static_assert(!has_key<no_key_graph>);
static_assert(!::bvn::dependency_graph<no_key_graph>);
static_assert(!::bvn::dependency_graph<missing_operations>);
static_assert(!::bvn::dependency_graph<missing_contains>);
static_assert(!::bvn::dependency_graph<missing_at>);
static_assert(!::bvn::dependency_graph<void_key>);
static_assert(!::bvn::dependency_graph<wrong_contains>);
static_assert(!::bvn::dependency_graph<explicit_contains>);
static_assert(!::bvn::dependency_graph<wrong_member_with_adl>);
static_assert(::bvn::dependency_graph<missing_emplace>);
static_assert(!::bvn::resolvable_dependency_graph<missing_emplace>);
static_assert(!can_resolve<no_key_graph> && !can_resolve<missing_emplace>);
static_assert(!::std::invocable<decltype(::bvn::contains), empty_graph&, int>);
static_assert(!::std::invocable<decltype(::bvn::emplace), empty_graph&, int>);
static_assert(!::std::invocable<decltype(::bvn::at), empty_graph&, int>);
static_assert(::bvn::resolvable_dependency_graph<lvalue_graph&>);
static_assert(!::bvn::dependency_graph<lvalue_graph>);
static_assert(::bvn::resolvable_dependency_graph<rvalue_graph>);
static_assert(!::bvn::dependency_graph<rvalue_graph&>);
static_assert(::bvn::resolvable_dependency_graph<adl_rvalue_graph>);
static_assert(!::bvn::dependency_graph<adl_rvalue_graph&>);
static_assert(::bvn::resolvable_dependency_graph<borrowed_key_graph&>);
static_assert(::bvn::resolvable_dependency_graph<void_result_graph>);
static_assert(::std::same_as<decltype(::bvn::resolve(::std::declval<void_result_graph&>(), 1)), void>);
static_assert(::std::same_as<decltype(::bvn::at(::std::declval<member_graph const&>(), 1)), int const&>);
static_assert(::std::same_as<decltype(::bvn::at(::std::declval<adl_graph const&>(), 1)), int const&>);
static_assert(::std::same_as<decltype(::bvn::emplace(::std::declval<adl_graph&>(), 1)), int&>);
static_assert(::std::same_as<decltype(::bvn::resolve(::std::declval<member_graph&>(), 1)), int&>);
static_assert(::std::same_as<decltype(::bvn::resolve(::std::declval<rvalue_graph>(), 1)), int&&>);
static_assert(::std::same_as<decltype(::bvn::resolve(::std::declval<adl_rvalue_graph>(), 1)), int&&>);
static_assert(noexcept(::bvn::resolve(::std::declval<member_graph&>(), 1)));
static_assert(noexcept(::bvn::resolve(::std::declval<adl_graph&>(), 1)));
static_assert(noexcept(::bvn::resolve(::std::declval<both_graph&>(), 1)));
static_assert(noexcept(::bvn::resolve(::std::declval<member_resolve_graph&>(), 1)));
static_assert(noexcept(::bvn::resolve(::std::declval<adl_resolve_graph&>(), 1)));
static_assert(!noexcept(::bvn::resolve(::std::declval<throwing_contains&>(), 1)));
static_assert(!noexcept(::bvn::resolve(::std::declval<throwing_emplace&>(), 1)));
static_assert(!noexcept(::bvn::resolve(::std::declval<throwing_at&>(), 1)));
static_assert(!noexcept(::bvn::resolve(::std::declval<throwing_member_resolve&>(), 1)));
static_assert(!noexcept(::bvn::resolve(::std::declval<throwing_adl_resolve&>(), 1)));
static_assert(::bvn::resolvable_dependency_graph<throwing_conversion_graph&>);
static_assert(!noexcept(::bvn::resolve(::std::declval<throwing_conversion_graph&>(), 1)));
static_assert(!noexcept(::bvn::resolve(::std::declval<member_graph&>(), ::std::declval<throwing_key>())));
static_assert(!::std::invocable<decltype(::bvn::resolve), member_graph&, empty_graph>);

constexpr bool check_dispatch(int key)
{
	auto member = member_graph{};
	auto&& first = ::bvn::resolve(member, key);
	auto&& second = ::bvn::resolve(member, key);
	if (&first != &member._value || &second != &first || first != key * 10 || member._builds != 1)
		return false;

	auto adl = adl_graph{};
	auto&& adl_first = ::bvn::resolve(adl, key);
	if (&adl_first != &adl._value || ::bvn::resolve(adl, key) != key * 20 || adl._builds != 1)
		return false;

	auto both = both_graph{};
	if (::bvn::resolve(both, key) != key * 10 || both._builds != 1)
		return false;

	auto custom_member = member_resolve_graph{};
	auto custom_adl = adl_resolve_graph{};
	if (::bvn::resolve(custom_member, key) != 123 || custom_member._builds != 0
		|| ::bvn::resolve(custom_adl, key) != 456 || custom_adl._builds != 0)
		return false;

	auto external = external_graph{};
	if (::bvn::resolve(external, key) != key * 20)
		return false;

	auto reference = reference_key_graph{};
	if (::bvn::resolve(::std::move(reference), key) != key * 10)
		return false;

	auto borrowed = borrowed_key_graph{};
	auto const immobile = immobile_key{key};
	if (::bvn::resolve(borrowed, immobile) != key * 10)
		return false;

	auto lvalue = lvalue_graph{};
	auto rvalue = rvalue_graph{};
	auto adl_rvalue = adl_rvalue_graph{};
	auto&& rvalue_result = ::bvn::resolve(::std::move(rvalue), key);
	if (::bvn::resolve(lvalue, key) != key * 10
		|| &rvalue_result != &rvalue._value || rvalue_result != key)
		return false;
	auto&& adl_result = ::bvn::resolve(::std::move(adl_rvalue), key);
	if (&adl_result != &adl_rvalue._value || adl_result != key)
		return false;

	auto read_only = missing_emplace{};
	return ::bvn::dependency_graph_tests::refinement(read_only) == 1
		&& ::bvn::dependency_graph_tests::refinement(member) == 2;
}

// 具体图演示递归构造与失败后的重试；通用 resolve 只操作公开原语。
struct recursive_graph
{
	using key_type = unsigned;

	bool contains(key_type key) const noexcept { return _built[key]; }
	int& at(key_type key) noexcept { assert(_built[key]); return _values[key]; }
	void emplace(key_type key)
	{
		assert(!_building[key]);
		_building[key] = true;
		try
		{
			_values[key] = key == 0 ? 10 : ::bvn::resolve(*this, key - 1) + 1;
			if (key == 2 && ::std::exchange(_fail_once, false))
				throw ::std::runtime_error{"construction failed"};
		}
		catch (...)
		{
			_building[key] = false;
			throw;
		}
		_building[key] = false;
		_built[key] = true;
		_order[_builds++] = key;
	}

	::std::array<bool, 3> _built{};
	::std::array<bool, 3> _building{};
	::std::array<int, 3> _values{};
	::std::array<key_type, 3> _order{};
	unsigned _builds = 0;
	bool _fail_once = true;
};

static_assert(::bvn::dependency_graph_tests::check_dispatch(4));
}

int main(int argc, char**)
{
	assert(::bvn::dependency_graph_tests::check_dispatch(argc));

	auto graph = ::bvn::dependency_graph_tests::recursive_graph{};
	auto failed = false;
	try
	{
		static_cast<void>(::bvn::resolve(graph, 2));
	}
	catch (::std::runtime_error const&)
	{
		failed = true;
	}
	assert(failed && !graph._building[2] && !graph._built[2] && graph._builds == 2);
	assert(::bvn::resolve(graph, 2) == 12 && graph._builds == 3);
	assert(::bvn::resolve(graph, 2) == 12 && graph._builds == 3);
	assert((graph._order == ::std::array<unsigned, 3>{0, 1, 2}));

	auto throwing = ::bvn::dependency_graph_tests::throwing_conversion_graph{};
	failed = false;
	try
	{
		static_cast<void>(::bvn::resolve(throwing, 1));
	}
	catch (::std::runtime_error const&)
	{
		failed = true;
	}
	assert(failed && throwing._builds == 0);

	auto key_graph = ::bvn::dependency_graph_tests::member_graph{};
	failed = false;
	try
	{
		static_cast<void>(::bvn::resolve(key_graph, ::bvn::dependency_graph_tests::throwing_key{}));
	}
	catch (::std::runtime_error const&)
	{
		failed = true;
	}
	assert(failed && key_graph._builds == 0);
}
