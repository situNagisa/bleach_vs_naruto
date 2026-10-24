# GLM（header-only）-> 目标 glm::glm
# 优先 $NAGISA_LIBRARY_ROOT/glm（git clone https://github.com/g-truc/glm），或 -DBVN_EXEC0_GLM_ROOT=...；
# 没有源码时退回 find_package(glm CONFIG)（vcpkg / 系统包）。
include("${CMAKE_CURRENT_LIST_DIR}/locate.cmake")

bvn_exec0_locate(BVN_EXEC0_GLM_ROOT
    ENV NAGISA_LIBRARY_ROOT
    SUBDIR glm
    PROBE glm/glm.hpp
    DOC "GLM 源码根目录（含 glm/glm.hpp）；留空则 find_package(glm)"
)

if(BVN_EXEC0_GLM_ROOT_FOUND)
    add_library(exec0_glm INTERFACE)
    target_include_directories(exec0_glm SYSTEM INTERFACE "${BVN_EXEC0_GLM_ROOT}")
    add_library(glm::glm ALIAS exec0_glm)
    message(STATUS "exec-0: GLM include = ${BVN_EXEC0_GLM_ROOT}")
else()
    find_package(glm CONFIG REQUIRED)
    message(STATUS "exec-0: GLM 来自 find_package = ${glm_DIR}")
endif()
