# demo/sdag-compose：把 sdag 的图降解成扇入 / 扇出算法拼成的普通 sender 树（扇入扇出可配置）。header-only，依赖 sdag。
# sdag 不认识本库。工具链、预设与 sdag 相同（见 ../static-dag）。
cmake_minimum_required(VERSION 4.4)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(sdag_compose LANGUAGES CXX)

# sdag：同仓库的兄弟目录，或 -DSDAG_COMPOSE_SDAG_DIR=...
set(SDAG_COMPOSE_SDAG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../static-dag" CACHE PATH "sdag 源码目录")
if(NOT TARGET sdag::sdag)
    add_subdirectory("${SDAG_COMPOSE_SDAG_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sdag")
endif()

add_library(sdag_compose INTERFACE)
add_library(sdag::compose ALIAS sdag_compose)
target_include_directories(sdag_compose INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(sdag_compose INTERFACE sdag::sdag)

option(SDAG_COMPOSE_BUILD_TESTS "构建 sdag-compose 的测试（作为子项目引入时默认关闭）" ${PROJECT_IS_TOP_LEVEL})
if(SDAG_COMPOSE_BUILD_TESTS)
    enable_testing()
    add_executable(sdag_compose_test test/compose_test.cpp)
    target_link_libraries(sdag_compose_test PRIVATE sdag::compose sdag::test_support)
    set_target_properties(sdag_compose_test PROPERTIES OUTPUT_NAME sdag-compose-test CXX_SCAN_FOR_MODULES OFF)
    add_test(NAME sdag_compose_test COMMAND sdag_compose_test)
endif()
