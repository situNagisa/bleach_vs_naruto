# demo/exec-0：bvn 第一个完全用 C++20 具名模块组织的 demo。
#
# 工具链锚定：只支持 Clang（Linux 上 clang++ + libstdc++（可选 libc++），Windows 上 clang-cl + MSVC STL），
# C++ 标准永远跟踪 CMake 认识的最新值（当前 26）。原因见 README.md 与 ../../bug/。
# 每个依赖 / 每类设置各占 cmake/ 下一个文件，本文件只负责把它们装配成 exec0_demo。
#
# 两种构建方式（README §3）：-DBVN_EXEC0_USE_MODULES=ON（默认）编译具名模块；
# OFF 时纯头文件构建，与 VS 的 DebugHeaders / ReleaseHeaders 配置是同一代码形态。
cmake_minimum_required(VERSION 4.4)

# MSVC 运行库选择（CMP0091）必须在 project() 之前；对 clang-cl 同样生效。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(bvn_exec0_demo LANGUAGES CXX)

option(BVN_EXEC0_USE_MODULES "用 C++ 具名模块构建（OFF：纯头文件构建）" ON)
message(STATUS "exec-0: BVN_EXEC0_USE_MODULES = ${BVN_EXEC0_USE_MODULES}")

include(cmake/toolchain_guard.cmake)
bvn_exec0_check_toolchain()

# ---------------------------------------------------------------- 语言与调试信息
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ${BVN_EXEC0_USE_MODULES})
# 所有配置都生成 PDB：编译 /Zi（clang-cl 即 -gcodeview），链接 /DEBUG 见 build_settings.cmake。
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase")

get_filename_component(BVN_EXEC0_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# ---------------------------------------------------------------- 设置与依赖
include(cmake/build_settings.cmake)
bvn_exec0_add_build_settings(exec0_build_settings)

if(BVN_EXEC0_USE_MODULES)
    include(cmake/std_module.cmake)
    bvn_exec0_add_std_module(exec0_std)
    target_link_libraries(exec0_std PUBLIC exec0_build_settings)
endif()

include(cmake/vulkan.cmake)     # Vulkan::Headers / Vulkan::Vulkan
include(cmake/glfw.cmake)       # glfw
include(cmake/stdexec.cmake)    # exec0_stdexec
include(cmake/nagisa.cmake)     # exec0_nagisa
include(cmake/vkfu.cmake)       # vkfu::vkfu
include(cmake/vkkl.cmake)       # exec0_vkkl
include(cmake/glm.cmake)        # glm::glm
find_package(Freetype REQUIRED)

# ---------------------------------------------------------------- demo 本体
# 源文件清单、模块依赖与两种模式的差异见 cmake/sources.cmake。
include(cmake/sources.cmake)
add_executable(exec0_demo)
bvn_exec0_add_demo_sources(exec0_demo)
if(BVN_EXEC0_USE_MODULES)
    target_link_libraries(exec0_demo PRIVATE exec0_std)
endif()
target_link_libraries(exec0_demo
    PRIVATE
        exec0_build_settings
        exec0_vkkl
        vkfu::vkfu
        exec0_stdexec
        exec0_nagisa
        glfw
        Vulkan::Vulkan
        glm::glm
        Freetype::Freetype
)
set_target_properties(exec0_demo PROPERTIES OUTPUT_NAME exec-0)
