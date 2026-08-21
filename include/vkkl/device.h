#pragma once

#include <expected>
#include <new>
#include <utility>

#include <vulkan/vulkan.h>

#include "buffer.h"
#include "buffer_view.h"
#include "command_pool.h"
#include "descriptor_pool.h"
#include "descriptor_set_layout.h"
#include "event.h"
#include "fence.h"
#include "framebuffer.h"
#include "image.h"
#include "image_view.h"
#include "pipeline.h"
#include "pipeline_cache.h"
#include "pipeline_layout.h"
#include "query_pool.h"
#include "render_pass.h"
#include "sampler.h"
#include "semaphore.h"
#include "shader_module.h"
#include "swapchain.h"

namespace vkkl
{
struct device_observer
{
	::VkDevice handle = VK_NULL_HANDLE;

	auto create_buffer(::VkBufferCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_buffer(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_buffer(::VkBufferCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<buffer, ::VkResult>
	{
		auto raw_buffer = ::VkBuffer{};
		if (auto result = ::vkCreateBuffer(handle, &create_info, allocation_callbacks, &raw_buffer); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return buffer{handle, raw_buffer, allocation_callbacks};
	}

	auto create_buffer_view(::VkBufferViewCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_buffer_view(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_buffer_view(::VkBufferViewCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<buffer_view, ::VkResult>
	{
		auto raw_buffer_view = ::VkBufferView{};
		if (auto result = ::vkCreateBufferView(handle, &create_info, allocation_callbacks, &raw_buffer_view); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return buffer_view{handle, raw_buffer_view, allocation_callbacks};
	}

	auto create_command_pool(::VkCommandPoolCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_command_pool(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_command_pool(::VkCommandPoolCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<command_pool, ::VkResult>
	{
		auto raw_command_pool = ::VkCommandPool{};
		if (auto result = ::vkCreateCommandPool(handle, &create_info, allocation_callbacks, &raw_command_pool); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return command_pool{handle, raw_command_pool, allocation_callbacks};
	}

	auto create_descriptor_pool(::VkDescriptorPoolCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_descriptor_pool(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_descriptor_pool(::VkDescriptorPoolCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<descriptor_pool, ::VkResult>
	{
		auto raw_descriptor_pool = ::VkDescriptorPool{};
		if (auto result = ::vkCreateDescriptorPool(handle, &create_info, allocation_callbacks, &raw_descriptor_pool); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return descriptor_pool{handle, raw_descriptor_pool, allocation_callbacks};
	}

	auto create_descriptor_set_layout(::VkDescriptorSetLayoutCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_descriptor_set_layout(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_descriptor_set_layout(::VkDescriptorSetLayoutCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<descriptor_set_layout, ::VkResult>
	{
		auto raw_descriptor_set_layout = ::VkDescriptorSetLayout{};
		if (auto result = ::vkCreateDescriptorSetLayout(handle, &create_info, allocation_callbacks, &raw_descriptor_set_layout); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return descriptor_set_layout{handle, raw_descriptor_set_layout, allocation_callbacks};
	}

	auto create_event(::VkEventCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_event(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_event(::VkEventCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<event, ::VkResult>
	{
		auto raw_event = ::VkEvent{};
		if (auto result = ::vkCreateEvent(handle, &create_info, allocation_callbacks, &raw_event); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return event{handle, raw_event, allocation_callbacks};
	}

	auto create_fence(::VkFenceCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_fence(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_fence(::VkFenceCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<fence, ::VkResult>
	{
		auto raw_fence = ::VkFence{};
		if (auto result = ::vkCreateFence(handle, &create_info, allocation_callbacks, &raw_fence); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return fence{handle, raw_fence, allocation_callbacks};
	}

	auto create_framebuffer(::VkFramebufferCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_framebuffer(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_framebuffer(::VkFramebufferCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<framebuffer, ::VkResult>
	{
		auto raw_framebuffer = ::VkFramebuffer{};
		if (auto result = ::vkCreateFramebuffer(handle, &create_info, allocation_callbacks, &raw_framebuffer); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return framebuffer{handle, raw_framebuffer, allocation_callbacks};
	}

	auto create_image(::VkImageCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_image(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_image(::VkImageCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<image, ::VkResult>
	{
		auto raw_image = ::VkImage{};
		if (auto result = ::vkCreateImage(handle, &create_info, allocation_callbacks, &raw_image); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return image{handle, raw_image, allocation_callbacks};
	}

	auto create_image_view(::VkImageViewCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_image_view(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_image_view(::VkImageViewCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<image_view, ::VkResult>
	{
		auto raw_image_view = ::VkImageView{};
		if (auto result = ::vkCreateImageView(handle, &create_info, allocation_callbacks, &raw_image_view); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return image_view{handle, raw_image_view, allocation_callbacks};
	}

	auto create_pipeline_cache(::VkPipelineCacheCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_pipeline_cache(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_pipeline_cache(::VkPipelineCacheCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<pipeline_cache, ::VkResult>
	{
		auto raw_pipeline_cache = ::VkPipelineCache{};
		if (auto result = ::vkCreatePipelineCache(handle, &create_info, allocation_callbacks, &raw_pipeline_cache); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return pipeline_cache{handle, raw_pipeline_cache, allocation_callbacks};
	}

	auto create_graphics_pipeline(::VkGraphicsPipelineCreateInfo const& create_info, ::VkPipelineCache pipeline_cache = VK_NULL_HANDLE, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_graphics_pipeline(create_info, pipeline_cache, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_graphics_pipeline(::VkGraphicsPipelineCreateInfo const& create_info, ::VkPipelineCache pipeline_cache, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<pipeline, ::VkResult>
	{
		auto raw_pipeline = ::VkPipeline{};
		if (auto result = ::vkCreateGraphicsPipelines(handle, pipeline_cache, 1, &create_info, allocation_callbacks, &raw_pipeline); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return pipeline{handle, raw_pipeline, allocation_callbacks};
	}

	auto create_compute_pipeline(::VkComputePipelineCreateInfo const& create_info, ::VkPipelineCache pipeline_cache = VK_NULL_HANDLE, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_compute_pipeline(create_info, pipeline_cache, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_compute_pipeline(::VkComputePipelineCreateInfo const& create_info, ::VkPipelineCache pipeline_cache, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<pipeline, ::VkResult>
	{
		auto raw_pipeline = ::VkPipeline{};
		if (auto result = ::vkCreateComputePipelines(handle, pipeline_cache, 1, &create_info, allocation_callbacks, &raw_pipeline); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return pipeline{handle, raw_pipeline, allocation_callbacks};
	}

	auto create_pipeline_layout(::VkPipelineLayoutCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_pipeline_layout(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_pipeline_layout(::VkPipelineLayoutCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<pipeline_layout, ::VkResult>
	{
		auto raw_pipeline_layout = ::VkPipelineLayout{};
		if (auto result = ::vkCreatePipelineLayout(handle, &create_info, allocation_callbacks, &raw_pipeline_layout); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return pipeline_layout{handle, raw_pipeline_layout, allocation_callbacks};
	}

	auto create_query_pool(::VkQueryPoolCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_query_pool(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_query_pool(::VkQueryPoolCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<query_pool, ::VkResult>
	{
		auto raw_query_pool = ::VkQueryPool{};
		if (auto result = ::vkCreateQueryPool(handle, &create_info, allocation_callbacks, &raw_query_pool); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return query_pool{handle, raw_query_pool, allocation_callbacks};
	}

	auto create_render_pass(::VkRenderPassCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_render_pass(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_render_pass(::VkRenderPassCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<render_pass, ::VkResult>
	{
		auto raw_render_pass = ::VkRenderPass{};
		if (auto result = ::vkCreateRenderPass(handle, &create_info, allocation_callbacks, &raw_render_pass); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return render_pass{handle, raw_render_pass, allocation_callbacks};
	}

	auto create_render_pass2(::VkRenderPassCreateInfo2 const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_render_pass2(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_render_pass2(::VkRenderPassCreateInfo2 const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<render_pass, ::VkResult>
	{
		auto raw_render_pass = ::VkRenderPass{};
		if (auto result = ::vkCreateRenderPass2(handle, &create_info, allocation_callbacks, &raw_render_pass); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return render_pass{handle, raw_render_pass, allocation_callbacks};
	}

	auto create_sampler(::VkSamplerCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_sampler(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_sampler(::VkSamplerCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<sampler, ::VkResult>
	{
		auto raw_sampler = ::VkSampler{};
		if (auto result = ::vkCreateSampler(handle, &create_info, allocation_callbacks, &raw_sampler); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return sampler{handle, raw_sampler, allocation_callbacks};
	}

	auto create_semaphore(::VkSemaphoreCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_semaphore(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_semaphore(::VkSemaphoreCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<semaphore, ::VkResult>
	{
		auto raw_semaphore = ::VkSemaphore{};
		if (auto result = ::vkCreateSemaphore(handle, &create_info, allocation_callbacks, &raw_semaphore); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return semaphore{handle, raw_semaphore, allocation_callbacks};
	}

	auto create_shader_module(::VkShaderModuleCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_shader_module(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_shader_module(::VkShaderModuleCreateInfo const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<shader_module, ::VkResult>
	{
		auto raw_shader_module = ::VkShaderModule{};
		if (auto result = ::vkCreateShaderModule(handle, &create_info, allocation_callbacks, &raw_shader_module); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return shader_module{handle, raw_shader_module, allocation_callbacks};
	}

	auto create_swapchain_khr(::VkSwapchainCreateInfoKHR const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_swapchain_khr(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_swapchain_khr(::VkSwapchainCreateInfoKHR const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<swapchain, ::VkResult>
	{
		auto raw_swapchain = ::VkSwapchainKHR{};
		if (auto result = ::vkCreateSwapchainKHR(handle, &create_info, allocation_callbacks, &raw_swapchain); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return swapchain{handle, raw_swapchain, allocation_callbacks};
	}

	auto create_shared_swapchain_khr(::VkSwapchainCreateInfoKHR const& create_info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) const
	{
		auto result = create_shared_swapchain_khr(create_info, allocation_callbacks, ::std::nothrow);
		if (!result) throw result.error();
		return *::std::move(result);
	}

	auto create_shared_swapchain_khr(::VkSwapchainCreateInfoKHR const& create_info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) const noexcept -> ::std::expected<swapchain, ::VkResult>
	{
		auto raw_swapchain = ::VkSwapchainKHR{};
		if (auto result = ::vkCreateSharedSwapchainsKHR(handle, 1, &create_info, allocation_callbacks, &raw_swapchain); result != ::VK_SUCCESS) return ::std::unexpected{result};
		return swapchain{handle, raw_swapchain, allocation_callbacks};
	}
};

struct device : device_observer
{
	::VkAllocationCallbacks const* allocation_callbacks = nullptr;

	constexpr device() noexcept = default;

	constexpr device(::VkDevice handle, ::VkAllocationCallbacks const* allocation_callbacks = nullptr) noexcept
		: device_observer{.handle = handle}
		, allocation_callbacks(allocation_callbacks)
	{}

	device(device const&) = delete;
	auto operator=(device const&) -> device& = delete;

	constexpr device(device&& other) noexcept
		: device_observer{.handle = ::std::exchange(other.handle, VK_NULL_HANDLE)}
		, allocation_callbacks(::std::exchange(other.allocation_callbacks, nullptr))
	{}

	constexpr auto&& operator=(device&& other) noexcept
	{
		if (this != &other)
		{
			[[maybe_unused]] auto temp = ::std::move(*this);
			handle = ::std::exchange(other.handle, VK_NULL_HANDLE);
			allocation_callbacks = ::std::exchange(other.allocation_callbacks, nullptr);
		}

		return *this;
	}

	~device() noexcept
	{
		if (handle != VK_NULL_HANDLE)
		{
			::vkDestroyDevice(handle, allocation_callbacks);
		}
	}
};
}
