# demo/static-dag：sdag —— 静态 DAG sender 模板库（header-only），完全独立于 bvn 主工程。
#
# 工具链：Clang >= 22 或 GCC >= 16，C++26，Ninja。编译期计算在支持静态反射的编译器上用反射（GCC 16 + -freflection），
# 否则用模板元编程；两份实现在同一文件里用 SDAG_REFLECTION 隔开，运行期代码共享。每个依赖 / 每类设置各占 cmake/ 下一个文件。
cmake_minimum_required(VERSION 4.4)

# MSVC 运行库选择（CMP0091）必须在 project() 之前；对 clang-cl 同样生效。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(sdag LANGUAGES CXX)

include(cmake/toolchain_guard.cmake)
sdag_check_toolchain()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase")

include(cmake/build_settings.cmake)
sdag_add_build_settings(sdag_build_settings)

include(cmake/stdexec.cmake) # sdag_stdexec

# ---------------------------------------------------------------- 库本体（header-only）
add_library(sdag INTERFACE)
add_library(sdag::sdag ALIAS sdag)
target_include_directories(sdag INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(sdag INTERFACE sdag_stdexec)
target_compile_features(sdag INTERFACE cxx_std_26)
sdag_add_reflection_flags(sdag)

# 测试辅助（check.h、fixtures.h）：扩展库的测试也可以用，但 sdag 不认识它们。
add_library(sdag_test_support INTERFACE)
add_library(sdag::test_support ALIAS sdag_test_support)
target_include_directories(sdag_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/test")
target_link_libraries(sdag_test_support INTERFACE sdag_build_settings)

# ---------------------------------------------------------------- 测试
option(SDAG_BUILD_TESTS "构建 sdag 的测试（作为子项目引入时默认关闭）" ${PROJECT_IS_TOP_LEVEL})
if(SDAG_BUILD_TESTS)
    enable_testing()

    function(sdag_add_test NAME SOURCE)
        add_executable(${NAME} ${SOURCE} test/check.h test/fixtures.h)
        target_link_libraries(${NAME} PRIVATE sdag::sdag sdag_build_settings)
        string(REPLACE "_" "-" _output "${NAME}")
        set_target_properties(${NAME} PROPERTIES OUTPUT_NAME ${_output} CXX_SCAN_FOR_MODULES OFF)
        add_test(NAME ${NAME} COMMAND ${NAME})
    endfunction()

    sdag_add_test(sdag_test test/test.cpp)
    sdag_add_test(sdag_zero_overhead test/zero_overhead.cpp)
    sdag_add_test(sdag_concurrency test/concurrency.cpp)
    # 常量求值测试：全部是 static_assert，编过即通过；运行时什么都不做。
    sdag_add_test(sdag_constexpr test/constexpr.cpp)
endif()
