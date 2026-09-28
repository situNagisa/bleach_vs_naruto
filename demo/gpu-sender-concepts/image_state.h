#pragma once

#include <concepts>

#include "./mock_vulkan.h"

/// image_state / buffer_state：Vulkan 专属的"状态长什么样"，跟 concepts.h 里的
/// tracked_resource 彻底分开——tracked_resource 只要求 state() 返回值满足
/// std::copyable，不关心里面有没有 layout。
///
/// buffer_state 只要求 stage()/access()；image_state 在此基础上多要求一个
/// layout()，是 buffer_state 的细化，不是跟它并列、互不相干的另一套字段集合——
/// 一个只有 stage/access 的类型天然满足 buffer_state，不满足 image_state（缺
/// layout）；一个 stage/access/layout 都有的类型两者都满足，这是应该的：它确实
/// "至少是 buffer 那么多信息"，只是多带了 layout。不存在"buffer 类型里有一个
/// 字段被忽略"的情况，因为 buffer_state 压根不要求那个字段存在。
///
/// 用成员函数而不是成员变量暴露 stage/access/layout：静态版本里它们是
/// static constexpr 函数，调用即编译期常量；动态版本里是普通只读访问器；两者对
/// 调用方是同一行代码 `from.stage()`，能不能被编译器常量折叠完全交给编译器决定，
/// 不需要为两种情况写两条判断分支。
namespace gpu
{
template <class S>
concept buffer_state = requires(S const& s)
{
	{ s.stage() } -> ::std::convertible_to<mock::pipeline_stage>;
	{ s.access() } -> ::std::convertible_to<mock::access>;
};

template <class S>
concept image_state = buffer_state<S> && requires(S const& s)
{
	{ s.layout() } -> ::std::convertible_to<mock::image_layout>;
};

// ---- image：编译期已知的状态是空类型，[[no_unique_address]] 能把它压到零大小 ----

template <mock::pipeline_stage Stage, mock::access Access, mock::image_layout Layout>
struct static_image_state
{
	static constexpr mock::pipeline_stage stage() noexcept { return Stage; }
	static constexpr mock::access access() noexcept { return Access; }
	static constexpr mock::image_layout layout() noexcept { return Layout; }

	bool operator==(static_image_state const&) const = default;
};

// ---- image：只能运行时确定的状态（比如从池子里借来的资源，历史状态只有池子知道） ----

struct dynamic_image_state
{
	mock::pipeline_stage _stage;
	mock::access _access;
	mock::image_layout _layout;

	constexpr mock::pipeline_stage stage() const noexcept { return _stage; }
	constexpr mock::access access() const noexcept { return _access; }
	constexpr mock::image_layout layout() const noexcept { return _layout; }

	bool operator==(dynamic_image_state const&) const = default;
};

using undefined = static_image_state<
	mock::pipeline_stage::top_of_pipe, mock::access::none, mock::image_layout::undefined>;
using color_attachment = static_image_state<
	mock::pipeline_stage::color_attachment_output, mock::access::color_attachment_write,
	mock::image_layout::color_attachment_optimal>;
using present_src = static_image_state<
	mock::pipeline_stage::bottom_of_pipe, mock::access::none, mock::image_layout::present_src>;

static_assert(image_state<undefined>);
static_assert(image_state<color_attachment>);
static_assert(image_state<present_src>);
static_assert(image_state<dynamic_image_state>);
static_assert(::std::is_empty_v<undefined> && ::std::is_empty_v<color_attachment> && ::std::is_empty_v<present_src>);

// ---- buffer：没有 layout 字段，用来确认这两个 concept 确实互斥，不是靠"忽略某字段"拼出来的 ----

template <mock::pipeline_stage Stage, mock::access Access>
struct static_buffer_state
{
	static constexpr mock::pipeline_stage stage() noexcept { return Stage; }
	static constexpr mock::access access() noexcept { return Access; }

	bool operator==(static_buffer_state const&) const = default;
};

using buffer_transfer_dst = static_buffer_state<mock::pipeline_stage::transfer, mock::access::transfer_write>;

static_assert(buffer_state<buffer_transfer_dst>);
static_assert(!image_state<buffer_transfer_dst>);   // 缺 layout()，不满足细化出来的 image_state
static_assert(buffer_state<color_attachment>);      // image_state 是 buffer_state 的细化，image 天然也满足它
}
