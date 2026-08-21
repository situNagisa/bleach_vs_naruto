#pragma once

#include <concepts>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

#include <bvn/graphics/environment.h>

NAGISA_BUILD_LIB_DETAIL_BEGIN

template <class R>
concept global_env_renderer = requires(R const& r)
{
	{ r.instance() } -> ::std::convertible_to<VkInstance const&>;
	{ r.physical_device() } -> ::std::convertible_to<VkPhysicalDevice const&>;
	{ r.device() } -> ::std::convertible_to<VkDevice const&>;
	{ r.graphics_queue() } -> ::std::convertible_to<VkQueue const&>;
	{ r.graphics_queue_family() } -> ::std::convertible_to<::std::uint32_t>;
	{ r.swapchain() } -> ::std::convertible_to<VkSwapchainKHR const&>;
	{ r.swapchain_extent() } -> ::std::convertible_to<VkExtent2D const&>;
	{ r.swapchain_image_format() } -> ::std::convertible_to<VkFormat const&>;
	{ r.swapchain_image_count() } -> ::std::convertible_to<::std::uint32_t>;
	{ r.swapchain_images() } -> ::std::same_as<::std::vector<VkImage>>;
	{ r.swapchain_image_views() } -> ::std::same_as<::std::vector<VkImageView>>;
	{ r.swapchain_render_finished() } -> ::std::same_as<::std::vector<VkSemaphore>>;
	{ r.depth_format() } -> ::std::convertible_to<VkFormat const&>;
	{ r.device_name() } -> ::std::convertible_to<char const*>;
};

template <class R>
concept frame_env_renderer = requires(R const& r)
{
	{ r.in_flight() } -> ::std::convertible_to<VkFence const&>;
	{ r.primary_command_pool() } -> ::std::convertible_to<VkCommandPool const&>;
	{ r.primary_command_buffer() } -> ::std::convertible_to<VkCommandBuffer const&>;
	{ r.image_available() } -> ::std::convertible_to<VkSemaphore const&>;
	{ r.render_finished() } -> ::std::convertible_to<VkSemaphore const&>;
	{ r.active_image_index() } -> ::std::convertible_to<::std::uint32_t>;
	{ r.active_image() } -> ::std::convertible_to<VkImage const&>;
	{ r.active_image_view() } -> ::std::convertible_to<VkImageView const&>;
	{ r.depth_image() } -> ::std::convertible_to<VkImage const&>;
	{ r.depth_image_view() } -> ::std::convertible_to<VkImageView const&>;
	{ r.extent() } -> ::std::convertible_to<VkExtent2D const&>;
};

namespace renderer_forward
{
	template <class T>
	constexpr decltype(auto) dereference(T&& value) noexcept
	{
		if constexpr (::std::is_pointer_v<::std::remove_reference_t<T>>)
		{
			return *value;
		}
		else
		{
			return ::std::forward<T>(value);
		}
	}
}

template <class R>
struct global_forward_env_renderer
{
	R _inner;

	constexpr explicit(false) global_forward_env_renderer(R inner)
		noexcept(::std::is_nothrow_constructible_v<R, R>)
		: _inner(::std::forward<R>(inner))
	{}

	constexpr decltype(auto) instance() const noexcept requires requires { renderer_forward::dereference(_inner).instance(); } { return renderer_forward::dereference(_inner).instance(); }
	constexpr decltype(auto) physical_device() const noexcept requires requires { renderer_forward::dereference(_inner).physical_device(); } { return renderer_forward::dereference(_inner).physical_device(); }
	constexpr decltype(auto) device() const noexcept requires requires { renderer_forward::dereference(_inner).device(); } { return renderer_forward::dereference(_inner).device(); }
	constexpr decltype(auto) graphics_queue() const noexcept requires requires { renderer_forward::dereference(_inner).graphics_queue(); } { return renderer_forward::dereference(_inner).graphics_queue(); }
	constexpr decltype(auto) graphics_queue_family() const noexcept requires requires { renderer_forward::dereference(_inner).graphics_queue_family(); } { return renderer_forward::dereference(_inner).graphics_queue_family(); }
	constexpr decltype(auto) swapchain() const noexcept requires requires { renderer_forward::dereference(_inner).swapchain(); } { return renderer_forward::dereference(_inner).swapchain(); }
	constexpr decltype(auto) swapchain_extent() const noexcept requires requires { renderer_forward::dereference(_inner).swapchain_extent(); } { return renderer_forward::dereference(_inner).swapchain_extent(); }
	constexpr decltype(auto) swapchain_image_format() const noexcept requires requires { renderer_forward::dereference(_inner).swapchain_image_format(); } { return renderer_forward::dereference(_inner).swapchain_image_format(); }
	constexpr decltype(auto) swapchain_image_count() const noexcept requires requires { renderer_forward::dereference(_inner).swapchain_image_count(); } { return renderer_forward::dereference(_inner).swapchain_image_count(); }
	constexpr decltype(auto) swapchain_images() const requires requires { renderer_forward::dereference(_inner).swapchain_images(); } { return renderer_forward::dereference(_inner).swapchain_images(); }
	constexpr decltype(auto) swapchain_image_views() const requires requires { renderer_forward::dereference(_inner).swapchain_image_views(); } { return renderer_forward::dereference(_inner).swapchain_image_views(); }
	constexpr decltype(auto) swapchain_render_finished() const requires requires { renderer_forward::dereference(_inner).swapchain_render_finished(); } { return renderer_forward::dereference(_inner).swapchain_render_finished(); }
	constexpr decltype(auto) depth_format() const noexcept requires requires { renderer_forward::dereference(_inner).depth_format(); } { return renderer_forward::dereference(_inner).depth_format(); }
	constexpr decltype(auto) device_name() const noexcept requires requires { renderer_forward::dereference(_inner).device_name(); } { return renderer_forward::dereference(_inner).device_name(); }
};

template <class R>
struct frame_forward_env_renderer
{
	R _inner;

	constexpr explicit(false) frame_forward_env_renderer(R inner)
		noexcept(::std::is_nothrow_constructible_v<R, R>)
		: _inner(::std::forward<R>(inner))
	{}

	constexpr auto&& handle() const noexcept
	requires requires { _inner; }
	{ return _inner; }

	constexpr decltype(auto) in_flight() const noexcept requires requires { renderer_forward::dereference(_inner).in_flight(); } { return renderer_forward::dereference(_inner).in_flight(); }
	constexpr decltype(auto) primary_command_pool() const noexcept requires requires { renderer_forward::dereference(_inner).primary_command_pool(); } { return renderer_forward::dereference(_inner).primary_command_pool(); }
	constexpr decltype(auto) primary_command_buffer() const noexcept requires requires { renderer_forward::dereference(_inner).primary_command_buffer(); } { return renderer_forward::dereference(_inner).primary_command_buffer(); }
	constexpr decltype(auto) image_available() const noexcept requires requires { renderer_forward::dereference(_inner).image_available(); } { return renderer_forward::dereference(_inner).image_available(); }
	constexpr decltype(auto) render_finished() const noexcept requires requires { renderer_forward::dereference(_inner).render_finished(); } { return renderer_forward::dereference(_inner).render_finished(); }
	constexpr decltype(auto) active_image_index() const noexcept requires requires { renderer_forward::dereference(_inner).active_image_index(); } { return renderer_forward::dereference(_inner).active_image_index(); }
	constexpr decltype(auto) active_image() const noexcept requires requires { renderer_forward::dereference(_inner).active_image(); } { return renderer_forward::dereference(_inner).active_image(); }
	constexpr decltype(auto) active_image_view() const noexcept requires requires { renderer_forward::dereference(_inner).active_image_view(); } { return renderer_forward::dereference(_inner).active_image_view(); }
	constexpr decltype(auto) depth_image() const noexcept requires requires { renderer_forward::dereference(_inner).depth_image(); } { return renderer_forward::dereference(_inner).depth_image(); }
	constexpr decltype(auto) depth_image_view() const noexcept requires requires { renderer_forward::dereference(_inner).depth_image_view(); } { return renderer_forward::dereference(_inner).depth_image_view(); }
	constexpr decltype(auto) extent() const noexcept requires requires { renderer_forward::dereference(_inner).extent(); } { return renderer_forward::dereference(_inner).extent(); }
};

namespace renderer_dynamic_forward
{
	struct basic_global_env_renderer
	{
		virtual ~basic_global_env_renderer() noexcept = default;

		virtual auto instance() const noexcept -> VkInstance = 0;
		virtual auto physical_device() const noexcept -> VkPhysicalDevice = 0;
		virtual auto device() const noexcept -> VkDevice = 0;
		virtual auto graphics_queue() const noexcept -> VkQueue = 0;
		virtual auto graphics_queue_family() const noexcept -> ::std::uint32_t = 0;
		virtual auto swapchain() const noexcept -> VkSwapchainKHR = 0;
		virtual auto swapchain_extent() const noexcept -> VkExtent2D = 0;
		virtual auto swapchain_image_format() const noexcept -> VkFormat = 0;
		virtual auto swapchain_image_count() const noexcept -> ::std::uint32_t = 0;
		virtual auto swapchain_images() const -> ::std::vector<VkImage> = 0;
		virtual auto swapchain_image_views() const -> ::std::vector<VkImageView> = 0;
		virtual auto swapchain_render_finished() const -> ::std::vector<VkSemaphore> = 0;
		virtual auto depth_format() const noexcept -> VkFormat = 0;
		virtual auto device_name() const noexcept -> char const* = 0;
	};

	struct basic_frame_env_renderer
	{
		virtual ~basic_frame_env_renderer() noexcept = default;

		virtual VkFence in_flight() const noexcept = 0;
		virtual VkCommandPool primary_command_pool() const noexcept = 0;
		virtual VkCommandBuffer primary_command_buffer() const noexcept = 0;
		virtual VkSemaphore image_available() const noexcept = 0;
		virtual VkSemaphore render_finished() const noexcept = 0;
		virtual ::std::uint32_t active_image_index() const noexcept = 0;
		virtual VkImage active_image() const noexcept = 0;
		virtual VkImageView active_image_view() const noexcept = 0;
		virtual VkImage depth_image() const noexcept = 0;
		virtual VkImageView depth_image_view() const noexcept = 0;
		virtual VkExtent2D extent() const noexcept = 0;
	};

	template <class R>
		requires requires(R r) { { renderer_forward::dereference(r) } -> global_env_renderer; }
	struct global_env_renderer_eraser : basic_global_env_renderer
	{
		R _inner;

		constexpr explicit(false) global_env_renderer_eraser(R inner)
			noexcept(::std::is_nothrow_constructible_v<R, R>)
			: _inner(::std::forward<R>(inner))
		{}

		constexpr virtual auto instance() const noexcept -> VkInstance override { return renderer_forward::dereference(_inner).instance(); }
		constexpr virtual auto physical_device() const noexcept -> VkPhysicalDevice override { return renderer_forward::dereference(_inner).physical_device(); }
		constexpr virtual auto device() const noexcept -> VkDevice override { return renderer_forward::dereference(_inner).device(); }
		constexpr virtual auto graphics_queue() const noexcept -> VkQueue override { return renderer_forward::dereference(_inner).graphics_queue(); }
		constexpr virtual auto graphics_queue_family() const noexcept -> ::std::uint32_t override { return renderer_forward::dereference(_inner).graphics_queue_family(); }
		constexpr virtual auto swapchain() const noexcept -> VkSwapchainKHR override { return renderer_forward::dereference(_inner).swapchain(); }
		constexpr virtual auto swapchain_extent() const noexcept -> VkExtent2D override { return renderer_forward::dereference(_inner).swapchain_extent(); }
		constexpr virtual auto swapchain_image_format() const noexcept -> VkFormat override { return renderer_forward::dereference(_inner).swapchain_image_format(); }
		constexpr virtual auto swapchain_image_count() const noexcept -> ::std::uint32_t override { return renderer_forward::dereference(_inner).swapchain_image_count(); }
		constexpr virtual auto swapchain_images() const -> ::std::vector<VkImage> override { return renderer_forward::dereference(_inner).swapchain_images(); }
		constexpr virtual auto swapchain_image_views() const -> ::std::vector<VkImageView> override { return renderer_forward::dereference(_inner).swapchain_image_views(); }
		constexpr virtual auto swapchain_render_finished() const -> ::std::vector<VkSemaphore> override { return renderer_forward::dereference(_inner).swapchain_render_finished(); }
		constexpr virtual auto depth_format() const noexcept -> VkFormat override { return renderer_forward::dereference(_inner).depth_format(); }
		constexpr virtual auto device_name() const noexcept -> char const* override { return renderer_forward::dereference(_inner).device_name(); }
	};

	template <class R>
		requires requires(R r) { { renderer_forward::dereference(r) } -> frame_env_renderer; }
	struct frame_env_renderer_eraser : basic_frame_env_renderer
	{
		R _inner;

		constexpr explicit(false) frame_env_renderer_eraser(R inner)
			noexcept(::std::is_nothrow_constructible_v<R, R>)
			: _inner(::std::forward<R>(inner))
		{}

		constexpr virtual auto in_flight() const noexcept -> VkFence override { return renderer_forward::dereference(_inner).in_flight(); }
		constexpr virtual auto primary_command_pool() const noexcept -> VkCommandPool override { return renderer_forward::dereference(_inner).primary_command_pool(); }
		constexpr virtual auto primary_command_buffer() const noexcept -> VkCommandBuffer override { return renderer_forward::dereference(_inner).primary_command_buffer(); }
		constexpr virtual auto image_available() const noexcept -> VkSemaphore override { return renderer_forward::dereference(_inner).image_available(); }
		constexpr virtual auto render_finished() const noexcept -> VkSemaphore override { return renderer_forward::dereference(_inner).render_finished(); }
		constexpr virtual auto active_image_index() const noexcept -> ::std::uint32_t override { return renderer_forward::dereference(_inner).active_image_index(); }
		constexpr virtual auto active_image() const noexcept -> VkImage override { return renderer_forward::dereference(_inner).active_image(); }
		constexpr virtual auto active_image_view() const noexcept -> VkImageView override { return renderer_forward::dereference(_inner).active_image_view(); }
		constexpr virtual auto depth_image() const noexcept -> VkImage override { return renderer_forward::dereference(_inner).depth_image(); }
		constexpr virtual auto depth_image_view() const noexcept -> VkImageView override { return renderer_forward::dereference(_inner).depth_image_view(); }
		constexpr virtual auto extent() const noexcept -> VkExtent2D override { return renderer_forward::dereference(_inner).extent(); }
	};
}

struct global_dynamic_forward_env_renderer
{
	::std::unique_ptr<renderer_dynamic_forward::basic_global_env_renderer> _inner;

	constexpr explicit(false) global_dynamic_forward_env_renderer() noexcept = default;

	constexpr explicit(false) global_dynamic_forward_env_renderer(::std::unique_ptr<renderer_dynamic_forward::basic_global_env_renderer> inner) noexcept
		: _inner(::std::move(inner))
	{}

	global_dynamic_forward_env_renderer(global_dynamic_forward_env_renderer const&) = delete;
	auto operator=(global_dynamic_forward_env_renderer const&) -> global_dynamic_forward_env_renderer& = delete;
	global_dynamic_forward_env_renderer(global_dynamic_forward_env_renderer&&) noexcept = default;
	auto operator=(global_dynamic_forward_env_renderer&&) noexcept -> global_dynamic_forward_env_renderer& = default;
	~global_dynamic_forward_env_renderer() noexcept = default;

	constexpr virtual auto instance() const noexcept -> VkInstance { return _inner->instance(); }
	constexpr virtual auto physical_device() const noexcept -> VkPhysicalDevice { return _inner->physical_device(); }
	constexpr virtual auto device() const noexcept -> VkDevice { return _inner->device(); }
	constexpr virtual auto graphics_queue() const noexcept -> VkQueue { return _inner->graphics_queue(); }
	constexpr virtual auto graphics_queue_family() const noexcept -> ::std::uint32_t { return _inner->graphics_queue_family(); }
	constexpr virtual auto swapchain() const noexcept -> VkSwapchainKHR { return _inner->swapchain(); }
	constexpr virtual auto swapchain_extent() const noexcept -> VkExtent2D { return _inner->swapchain_extent(); }
	constexpr virtual auto swapchain_image_format() const noexcept -> VkFormat { return _inner->swapchain_image_format(); }
	constexpr virtual auto swapchain_image_count() const noexcept -> ::std::uint32_t { return _inner->swapchain_image_count(); }
	constexpr virtual auto swapchain_images() const -> ::std::vector<VkImage> { return _inner->swapchain_images(); }
	constexpr virtual auto swapchain_image_views() const -> ::std::vector<VkImageView> { return _inner->swapchain_image_views(); }
	constexpr virtual auto swapchain_render_finished() const -> ::std::vector<VkSemaphore> { return _inner->swapchain_render_finished(); }
	constexpr virtual auto depth_format() const noexcept -> VkFormat { return _inner->depth_format(); }
	constexpr virtual auto device_name() const noexcept -> char const* { return _inner->device_name(); }
};

struct frame_dynamic_forward_env_renderer
{
	::std::unique_ptr<renderer_dynamic_forward::basic_frame_env_renderer> _inner;

	constexpr explicit(false) frame_dynamic_forward_env_renderer() noexcept = default;

	constexpr explicit(false) frame_dynamic_forward_env_renderer(::std::unique_ptr<renderer_dynamic_forward::basic_frame_env_renderer> inner) noexcept
		: _inner(::std::move(inner))
	{}

	frame_dynamic_forward_env_renderer(frame_dynamic_forward_env_renderer const&) = delete;
	auto operator=(frame_dynamic_forward_env_renderer const&) -> frame_dynamic_forward_env_renderer& = delete;
	frame_dynamic_forward_env_renderer(frame_dynamic_forward_env_renderer&&) noexcept = default;
	auto operator=(frame_dynamic_forward_env_renderer&&) noexcept -> frame_dynamic_forward_env_renderer& = default;
	~frame_dynamic_forward_env_renderer() noexcept = default;

	auto in_flight() const noexcept -> VkFence { return _inner->in_flight(); }
	auto primary_command_pool() const noexcept -> VkCommandPool { return _inner->primary_command_pool(); }
	auto primary_command_buffer() const noexcept -> VkCommandBuffer { return _inner->primary_command_buffer(); }
	auto image_available() const noexcept -> VkSemaphore { return _inner->image_available(); }
	auto render_finished() const noexcept -> VkSemaphore { return _inner->render_finished(); }
	auto active_image_index() const noexcept -> ::std::uint32_t { return _inner->active_image_index(); }
	auto active_image() const noexcept -> VkImage { return _inner->active_image(); }
	auto active_image_view() const noexcept -> VkImageView { return _inner->active_image_view(); }
	auto depth_image() const noexcept -> VkImage { return _inner->depth_image(); }
	auto depth_image_view() const noexcept -> VkImageView { return _inner->depth_image_view(); }
	auto extent() const noexcept -> VkExtent2D { return _inner->extent(); }
};

template <class R>
	requires ::std::constructible_from<renderer_dynamic_forward::global_env_renderer_eraser<R>, R&&>
auto dynamic_forward_global_env_renderer(R&& renderer)
{
	if constexpr (::std::is_pointer_v<::std::remove_cvref_t<R>>)
	{
		return global_dynamic_forward_env_renderer{ ::std::make_unique<renderer_dynamic_forward::global_env_renderer_eraser<::std::remove_cvref_t<R>>>(renderer) };
	}
	else
	{
		return global_dynamic_forward_env_renderer{ ::std::make_unique<renderer_dynamic_forward::global_env_renderer_eraser<R>>(::std::forward<R>(renderer)) };
	}
}

template <class R>
	requires ::std::constructible_from<renderer_dynamic_forward::frame_env_renderer_eraser<R>, R>
auto dynamic_forward_frame_env_renderer(R&& renderer)
{
	if constexpr (::std::is_pointer_v<::std::remove_cvref_t<R>>)
	{
		return frame_dynamic_forward_env_renderer{ ::std::make_unique<renderer_dynamic_forward::frame_env_renderer_eraser<::std::remove_cvref_t<R>>>(renderer) };
	}
	else
	{
		return frame_dynamic_forward_env_renderer{ ::std::make_unique<renderer_dynamic_forward::frame_env_renderer_eraser<R>>(::std::forward<R>(renderer)) };
	}
}

static_assert(global_env_renderer<global_dynamic_forward_env_renderer>);
static_assert(frame_env_renderer<frame_dynamic_forward_env_renderer>);

NAGISA_BUILD_LIB_DETAIL_END

NAGISA_BUILD_LIB_BEGIN

using details::dynamic_forward_frame_env_renderer;
using details::dynamic_forward_global_env_renderer;
using details::frame_dynamic_forward_env_renderer;
using details::frame_env_renderer;
using details::frame_forward_env_renderer;
using details::global_dynamic_forward_env_renderer;
using details::global_env_renderer;
using details::global_forward_env_renderer;

NAGISA_BUILD_LIB_END

#include <nagisa/build_lib/destruct.h>
