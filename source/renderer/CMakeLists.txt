find_package(Vulkan REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)

bvn_add_static_module(bvn_renderer renderer)
target_link_libraries(bvn_renderer
	PUBLIC
		bvn_platform
		Vulkan::Vulkan
		vk-bootstrap::vk-bootstrap
)
target_compile_definitions(bvn_renderer
	PRIVATE
		BVN_RENDERER_BUILD
)
