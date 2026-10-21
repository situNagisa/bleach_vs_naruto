# vkkl（header-only，仓库内 include/vkkl）-> 目标 exec0_vkkl
add_library(exec0_vkkl INTERFACE)
target_include_directories(exec0_vkkl INTERFACE "${BVN_EXEC0_REPO_ROOT}/include")
target_link_libraries(exec0_vkkl INTERFACE Vulkan::Vulkan)
