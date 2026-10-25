# demo/exec-0：bvn 第一个完全用 C++20 具名模块组织的 demo。
#
# 工具链锚定：只支持 Clang（Linux 上 clang++ + libc++，Windows 上 clang-cl + MSVC STL），
# C++ 标准永远跟踪 CMake 认识的最新值（当前 26）。原因见 README.md 与 ../../bug/。
# 每个依赖 / 每类设置各占 cmake/ 下一个文件，本文件只负责把它们装配成 exec0_demo。
cmake_minimum_required(VERSION 4.4)

# MSVC 运行库选择（CMP0091）必须在 project() 之前；对 clang-cl 同样生效。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(bvn_exec0_demo LANGUAGES CXX)

include(cmake/toolchain_guard.cmake)
bvn_exec0_check_toolchain()

# ---------------------------------------------------------------- 语言与调试信息
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
# 所有配置都生成 PDB：编译 /Zi（clang-cl 即 -gcodeview），链接 /DEBUG 见 build_settings.cmake。
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase")

get_filename_component(BVN_EXEC0_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# ---------------------------------------------------------------- 设置与依赖
include(cmake/build_settings.cmake)
bvn_exec0_add_build_settings(exec0_build_settings)

include(cmake/std_module.cmake)
bvn_exec0_add_std_module(exec0_std)
target_link_libraries(exec0_std PUBLIC exec0_build_settings)

include(cmake/vulkan.cmake)     # Vulkan::Headers / Vulkan::Vulkan
include(cmake/glfw.cmake)       # glfw
include(cmake/stdexec.cmake)    # exec0_stdexec
include(cmake/nagisa.cmake)     # exec0_nagisa
include(cmake/sdag.cmake)       # exec0_sdag
include(cmake/vkfu.cmake)       # vkfu::vkfu
include(cmake/vkkl.cmake)       # exec0_vkkl
include(cmake/glm.cmake)       # glm::glm
find_package(Freetype REQUIRED)

# ---------------------------------------------------------------- demo 本体
# 模块依赖：std <- demo.execution <- demo.input  <- main
#           std <- demo.window    <- demo.input
#                  demo.window    <- demo.render <- main
#                  demo.execution <- demo.render
add_executable(exec0_demo)
target_sources(exec0_demo
    PRIVATE
        main.cpp
        window/window.cpp
        input/input.cpp
        render/renderer.cpp
    PRIVATE FILE_SET CXX_MODULES FILES
        execution/execution.cppm
        window/window.cppm
        input/input.cppm
        render/renderer.cppm
)
target_link_libraries(exec0_demo
    PRIVATE
        exec0_build_settings
        exec0_std
        exec0_vkkl
        vkfu::vkfu
        exec0_stdexec
        exec0_nagisa
        exec0_sdag
        glfw
        Vulkan::Vulkan
        glm::glm
        Freetype::Freetype
)
set_target_properties(exec0_demo PROPERTIES OUTPUT_NAME exec-0)
