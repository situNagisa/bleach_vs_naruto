# demo/sdag-adaptive：自适配图——平时融合，所在 domain 定制了 when_all 时自动降解。header-only，依赖 sdag 与 sdag-compose。
# sdag、sdag-compose 都不认识本库。工具链、预设与 sdag 相同（见 ../static-dag）。
cmake_minimum_required(VERSION 4.4)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(sdag_adaptive LANGUAGES CXX)

# sdag-compose（它会带进 sdag）：同仓库的兄弟目录，或 -DSDAG_ADAPTIVE_COMPOSE_DIR=...
set(SDAG_ADAPTIVE_COMPOSE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdag-compose" CACHE PATH "sdag-compose 源码目录")
if(NOT TARGET sdag::compose)
    add_subdirectory("${SDAG_ADAPTIVE_COMPOSE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sdag-compose")
endif()

add_library(sdag_adaptive INTERFACE)
add_library(sdag::adaptive ALIAS sdag_adaptive)
target_include_directories(sdag_adaptive INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(sdag_adaptive INTERFACE sdag::compose)

option(SDAG_ADAPTIVE_BUILD_TESTS "构建 sdag-adaptive 的测试（作为子项目引入时默认关闭）" ${PROJECT_IS_TOP_LEVEL})
if(SDAG_ADAPTIVE_BUILD_TESTS)
    enable_testing()
    add_executable(sdag_adaptive_test test/adaptive_test.cpp)
    target_link_libraries(sdag_adaptive_test PRIVATE sdag::adaptive sdag::test_support)
    set_target_properties(sdag_adaptive_test PROPERTIES OUTPUT_NAME sdag-adaptive-test CXX_SCAN_FOR_MODULES OFF)
    add_test(NAME sdag_adaptive_test COMMAND sdag_adaptive_test)
endif()
