# Vulkan 头文件与 loader -> Vulkan::Headers / Vulkan::Vulkan（CMake 自带的 FindVulkan）
# Windows：安装 Vulkan SDK（环境变量 VULKAN_SDK）；Linux：vulkan-headers + vulkan-icd-loader。
find_package(Vulkan REQUIRED)
message(STATUS "exec-0: Vulkan = ${Vulkan_LIBRARY} (headers ${Vulkan_VERSION})")
