# demo/static-dag：sdag —— 静态 DAG sender 模板库（header-only），完全独立于 bvn 主工程。
#
# 工具链：编译期计算全部用 C++26 静态反射（P2996），目前只有 GCC >= 16 的 -freflection 实现了它。
# 每个依赖 / 每类设置各占 cmake/ 下一个文件。
cmake_minimum_required(VERSION 4.4)

project(sdag LANGUAGES CXX)

include(cmake/toolchain_guard.cmake)
sdag_check_toolchain()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/build_settings.cmake)
sdag_add_build_settings(sdag_build_settings)

include(cmake/stdexec.cmake) # sdag_stdexec

# ---------------------------------------------------------------- 库本体（header-only）
add_library(sdag INTERFACE)
add_library(sdag::sdag ALIAS sdag)
target_include_directories(sdag INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(sdag INTERFACE sdag_stdexec)
target_compile_features(sdag INTERFACE cxx_std_26)
target_compile_options(sdag INTERFACE -freflection)

# ---------------------------------------------------------------- 测试
option(SDAG_BUILD_TESTS "构建 sdag 的测试" ON)
if(SDAG_BUILD_TESTS)
    enable_testing()

    add_executable(sdag_test test/test.cpp test/check.h)
    target_link_libraries(sdag_test PRIVATE sdag::sdag sdag_build_settings)
    set_target_properties(sdag_test PROPERTIES OUTPUT_NAME sdag-test CXX_SCAN_FOR_MODULES OFF)
    add_test(NAME sdag.test COMMAND sdag_test)

    add_executable(sdag_zero_overhead test/zero_overhead.cpp test/check.h)
    target_link_libraries(sdag_zero_overhead PRIVATE sdag::sdag sdag_build_settings)
    set_target_properties(sdag_zero_overhead PROPERTIES OUTPUT_NAME sdag-zero-overhead CXX_SCAN_FOR_MODULES OFF)
    add_test(NAME sdag.zero_overhead COMMAND sdag_zero_overhead)
endif()
