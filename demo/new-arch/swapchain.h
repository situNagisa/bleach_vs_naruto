#pragma once

#include <vulkan/vulkan.h>
#include "./demo_vulkan.h"

struct swapchain_observer
{
	::VkSwapchainKHR handle = VK_NULL_HANDLE;


};

struct swapchain : swapchain_observer
{
	::std::vector<::VkImage> _images{};
	::std::vector<::VkImageView> _views{};


};