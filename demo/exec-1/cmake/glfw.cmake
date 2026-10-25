# GLFW -> 目标 glfw
# 优先从 $NAGISA_LIBRARY_ROOT/glfw（或 -DBVN_EXEC0_GLFW_ROOT=...）的源码静态构建；
# 没有源码时退回 find_package(glfw3 CONFIG)（系统包）。
include("${CMAKE_CURRENT_LIST_DIR}/locate.cmake")

bvn_exec0_locate(BVN_EXEC0_GLFW_ROOT
    ENV NAGISA_LIBRARY_ROOT
    SUBDIR glfw
    PROBE include/GLFW/glfw3.h
    DOC "GLFW 源码根目录（含 include/GLFW/glfw3.h）；留空则 find_package(glfw3)"
)

if(BVN_EXEC0_GLFW_ROOT_FOUND)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
    add_subdirectory("${BVN_EXEC0_GLFW_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)
    message(STATUS "exec-0: GLFW 从源码静态构建 = ${BVN_EXEC0_GLFW_ROOT}")
else()
    find_package(glfw3 CONFIG REQUIRED)
    message(STATUS "exec-0: GLFW 来自 find_package = ${glfw3_DIR}")
endif()
