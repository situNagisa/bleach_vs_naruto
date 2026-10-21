# vkfu（header-only，仓库内 ../../vkfu）-> 目标 vkfu::vkfu；它自己会 find_package(Vulkan)。
set(VKFU_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VKFU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${BVN_EXEC0_REPO_ROOT}/vkfu" "${CMAKE_CURRENT_BINARY_DIR}/vkfu" EXCLUDE_FROM_ALL)
