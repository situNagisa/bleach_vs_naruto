find_package(Vulkan REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)

bvn_add_static_module(bvn_graphics graphics)
target_link_libraries(bvn_graphics
	PUBLIC
		bvn_platform
		bvn_nagisa_headers
		STDEXEC::stdexec
		Vulkan::Vulkan
		vk-bootstrap::vk-bootstrap
)
