# stdexec（header-only）-> 目标 sdag_stdexec
# 定位：$NAGISA_LIBRARY_ROOT/stdexec，或 -DSDAG_STDEXEC_ROOT=...；都没有时 FetchContent 拉取固定提交。
include("${CMAKE_CURRENT_LIST_DIR}/locate.cmake")

find_package(Threads REQUIRED)

sdag_locate(SDAG_STDEXEC_ROOT
    ENV NAGISA_LIBRARY_ROOT
    SUBDIR stdexec
    PROBE include/stdexec/execution.hpp
    DOC "stdexec 源码根目录（含 include/stdexec/execution.hpp）；留空则用 FetchContent 拉取固定提交"
)

if(SDAG_STDEXEC_ROOT_FOUND)
    set(_sdag_stdexec_include "${SDAG_STDEXEC_ROOT}/include")
else()
    include(FetchContent)
    # 本库在此提交上验证过（2026-10-20）；只取源码，不跑 stdexec 自己的 CMake。
    FetchContent_Declare(sdag_stdexec_source
        GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
        GIT_TAG 8398d5a547b2ce1c5168336a9123cce8be9ab7f6
        SOURCE_SUBDIR cmake-is-not-used
    )
    FetchContent_MakeAvailable(sdag_stdexec_source)
    set(_sdag_stdexec_include "${sdag_stdexec_source_SOURCE_DIR}/include")
endif()

add_library(sdag_stdexec INTERFACE)
target_include_directories(sdag_stdexec SYSTEM INTERFACE "${_sdag_stdexec_include}")
target_link_libraries(sdag_stdexec INTERFACE Threads::Threads)
message(STATUS "sdag: stdexec include = ${_sdag_stdexec_include}")
