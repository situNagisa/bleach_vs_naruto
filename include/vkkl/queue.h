#pragma once

#include <vulkan/vulkan.h>

namespace vkkl
{
struct queue_observer
{
	::VkQueue handle = VK_NULL_HANDLE;
};
}
