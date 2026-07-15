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
	{ r.image_available() } -> ::std::convertible_to<VkSemaphore const&>;
	{ r.active_image_index() } -> ::std::convertible_to<::std::uint32_t>;
	{ r.depth_image() } -> ::std::convertible_to<VkImage const&>;
	{ r.depth_image_view() } -> ::std::convertible_to<VkImageView const&>;
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

	constexpr auto instance() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).instance();
	}

	constexpr auto physical_device() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).physical_device();
	}

	constexpr auto device() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).device();
	}

	constexpr auto graphics_queue() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).graphics_queue();
	}

	constexpr auto graphics_queue_family() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).graphics_queue_family();
	}

	constexpr auto swapchain() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).swapchain();
	}

	constexpr auto swapchain_extent() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).swapchain_extent();
	}

	constexpr auto swapchain_image_format() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).swapchain_image_format();
	}

	constexpr auto swapchain_image_count() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).swapchain_image_count();
	}

	constexpr auto swapchain_images() const -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).swapchain_images();
	}

	constexpr auto swapchain_image_views() const -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).swapchain_image_views();
	}

	constexpr auto swapchain_render_finished() const -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).swapchain_render_finished();
	}

	constexpr auto depth_format() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).depth_format();
	}

	constexpr auto device_name() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).device_name();
	}
};

template <class R>
struct frame_forward_env_renderer
{
	R _inner;

	constexpr explicit(false) frame_forward_env_renderer(R inner)
		noexcept(::std::is_nothrow_constructible_v<R, R>)
		: _inner(::std::forward<R>(inner))
	{}

	constexpr auto in_flight() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).in_flight();
	}

	constexpr auto image_available() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).image_available();
	}

	constexpr auto active_image_index() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).active_image_index();
	}

	constexpr auto depth_image() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).depth_image();
	}

	constexpr auto depth_image_view() const noexcept -> decltype(auto)
	{
		return renderer_forward::dereference(_inner).depth_image_view();
	}
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

		virtual auto in_flight() const noexcept -> VkFence = 0;
		virtual auto image_available() const noexcept -> VkSemaphore = 0;
		virtual auto active_image_index() const noexcept -> ::std::uint32_t = 0;
		virtual auto depth_image() const noexcept -> VkImage = 0;
		virtual auto depth_image_view() const noexcept -> VkImageView = 0;
	};

	template <class R>
	struct global_env_renderer_eraser : basic_global_env_renderer
	{
		R _inner;

		constexpr explicit(false) global_env_renderer_eraser(R inner)
			noexcept(::std::is_nothrow_constructible_v<R, R>)
			: _inner(::std::forward<R>(inner))
		{}

		auto instance() const noexcept -> VkInstance override
		{
			return renderer_forward::dereference(_inner).instance();
		}

		auto physical_device() const noexcept -> VkPhysicalDevice override
		{
			return renderer_forward::dereference(_inner).physical_device();
		}

		auto device() const noexcept -> VkDevice override
		{
			return renderer_forward::dereference(_inner).device();
		}

		auto graphics_queue() const noexcept -> VkQueue override
		{
			return renderer_forward::dereference(_inner).graphics_queue();
		}

		auto graphics_queue_family() const noexcept -> ::std::uint32_t override
		{
			return renderer_forward::dereference(_inner).graphics_queue_family();
		}

		auto swapchain() const noexcept -> VkSwapchainKHR override
		{
			return renderer_forward::dereference(_inner).swapchain();
		}

		auto swapchain_extent() const noexcept -> VkExtent2D override
		{
			return renderer_forward::dereference(_inner).swapchain_extent();
		}

		auto swapchain_image_format() const noexcept -> VkFormat override
		{
			return renderer_forward::dereference(_inner).swapchain_image_format();
		}

		auto swapchain_image_count() const noexcept -> ::std::uint32_t override
		{
			return renderer_forward::dereference(_inner).swapchain_image_count();
		}

		auto swapchain_images() const -> ::std::vector<VkImage> override
		{
			return renderer_forward::dereference(_inner).swapchain_images();
		}

		auto swapchain_image_views() const -> ::std::vector<VkImageView> override
		{
			return renderer_forward::dereference(_inner).swapchain_image_views();
		}

		auto swapchain_render_finished() const -> ::std::vector<VkSemaphore> override
		{
			return renderer_forward::dereference(_inner).swapchain_render_finished();
		}

		auto depth_format() const noexcept -> VkFormat override
		{
			return renderer_forward::dereference(_inner).depth_format();
		}

		auto device_name() const noexcept -> char const* override
		{
			return renderer_forward::dereference(_inner).device_name();
		}
	};

	template <class R>
	struct frame_env_renderer_eraser : basic_frame_env_renderer
	{
		R _inner;

		constexpr explicit(false) frame_env_renderer_eraser(R inner)
			noexcept(::std::is_nothrow_constructible_v<R, R>)
			: _inner(::std::forward<R>(inner))
		{}

		auto in_flight() const noexcept -> VkFence override
		{
			return renderer_forward::dereference(_inner).in_flight();
		}

		auto image_available() const noexcept -> VkSemaphore override
		{
			return renderer_forward::dereference(_inner).image_available();
		}

		auto active_image_index() const noexcept -> ::std::uint32_t override
		{
			return renderer_forward::dereference(_inner).active_image_index();
		}

		auto depth_image() const noexcept -> VkImage override
		{
			return renderer_forward::dereference(_inner).depth_image();
		}

		auto depth_image_view() const noexcept -> VkImageView override
		{
			return renderer_forward::dereference(_inner).depth_image_view();
		}
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

	auto instance() const noexcept -> VkInstance
	{
		return _inner->instance();
	}

	auto physical_device() const noexcept -> VkPhysicalDevice
	{
		return _inner->physical_device();
	}

	auto device() const noexcept -> VkDevice
	{
		return _inner->device();
	}

	auto graphics_queue() const noexcept -> VkQueue
	{
		return _inner->graphics_queue();
	}

	auto graphics_queue_family() const noexcept -> ::std::uint32_t
	{
		return _inner->graphics_queue_family();
	}

	auto swapchain() const noexcept -> VkSwapchainKHR
	{
		return _inner->swapchain();
	}

	auto swapchain_extent() const noexcept -> VkExtent2D
	{
		return _inner->swapchain_extent();
	}

	auto swapchain_image_format() const noexcept -> VkFormat
	{
		return _inner->swapchain_image_format();
	}

	auto swapchain_image_count() const noexcept -> ::std::uint32_t
	{
		return _inner->swapchain_image_count();
	}

	auto swapchain_images() const -> ::std::vector<VkImage>
	{
		return _inner->swapchain_images();
	}

	auto swapchain_image_views() const -> ::std::vector<VkImageView>
	{
		return _inner->swapchain_image_views();
	}

	auto swapchain_render_finished() const -> ::std::vector<VkSemaphore>
	{
		return _inner->swapchain_render_finished();
	}

	auto depth_format() const noexcept -> VkFormat
	{
		return _inner->depth_format();
	}

	auto device_name() const noexcept -> char const*
	{
		return _inner->device_name();
	}
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

	auto in_flight() const noexcept -> VkFence
	{
		return _inner->in_flight();
	}

	auto image_available() const noexcept -> VkSemaphore
	{
		return _inner->image_available();
	}

	auto active_image_index() const noexcept -> ::std::uint32_t
	{
		return _inner->active_image_index();
	}

	auto depth_image() const noexcept -> VkImage
	{
		return _inner->depth_image();
	}

	auto depth_image_view() const noexcept -> VkImageView
	{
		return _inner->depth_image_view();
	}
};

template <global_env_renderer R>
auto dynamic_forward_global_env_renderer(R&& renderer) -> global_dynamic_forward_env_renderer
{
	using renderer_type = ::std::remove_cvref_t<R>;
	return {::std::make_unique<renderer_dynamic_forward::global_env_renderer_eraser<renderer_type>>(::std::forward<R>(renderer))};
}

template <frame_env_renderer R>
auto dynamic_forward_frame_env_renderer(R&& renderer) -> frame_dynamic_forward_env_renderer
{
	using renderer_type = ::std::remove_cvref_t<R>;
	return {::std::make_unique<renderer_dynamic_forward::frame_env_renderer_eraser<renderer_type>>(::std::forward<R>(renderer))};
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
