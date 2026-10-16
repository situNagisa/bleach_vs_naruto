#pragma once

#include <array>
#include <functional>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/split.hpp>

#include <nagisa/concurrency/lease.h>

#include "../framework/frame_context.h"
#include "../framework/frame_slot.h"
#include "../framework/any_sender.h"

#include "./detail/demo_vulkan.h"

struct renderer
{
	using resource_pool = ::nagisa::concurrency::bounded_lease_pool<frame_slot*, 4>;
	using lease = resource_pool::lease;

	[[nodiscard]] values_sender_type<lease> begin();
	[[nodiscard]] values_sender_type<> end(frame_slot& frame, ::std::span<::VkCommandBuffer const> commands);

	renderer();

	void set_fullscreen(bool enabled);

	::bvn::platform::window window;
	::consumer_arch_vulkan::vulkan_context vulkan;
	::std::array<frame_slot, 4> _frames;
	resource_pool _pool;
};
