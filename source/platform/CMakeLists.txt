find_package(SDL3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)

bvn_add_static_module(bvn_platform platform)
target_link_libraries(bvn_platform PUBLIC SDL3::SDL3 Vulkan::Headers)
target_compile_definitions(bvn_platform PRIVATE BVN_PLATFORM_BUILD)
