find_package(Vulkan REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

bvn_add_static_module(bvn_rhi rhi)
target_link_libraries(bvn_rhi
	PUBLIC
		Vulkan::Vulkan
		vk-bootstrap::vk-bootstrap
		GPUOpen::VulkanMemoryAllocator
)
target_compile_definitions(bvn_rhi
	PRIVATE
		BVN_RHI_BUILD
)
