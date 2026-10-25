# stdexec（header-only）-> 目标 exec0_stdexec
# 定位：$NAGISA_LIBRARY_ROOT/stdexec，或 -DBVN_EXEC0_STDEXEC_ROOT=...；都没有时 FetchContent 拉取固定提交。
include("${CMAKE_CURRENT_LIST_DIR}/locate.cmake")

find_package(Threads REQUIRED)

bvn_exec0_locate(BVN_EXEC0_STDEXEC_ROOT
    ENV NAGISA_LIBRARY_ROOT
    SUBDIR stdexec
    PROBE include/stdexec/execution.hpp
    DOC "stdexec 源码根目录（含 include/stdexec/execution.hpp）；留空则用 FetchContent 拉取固定提交"
)

if(BVN_EXEC0_STDEXEC_ROOT_FOUND)
    set(_exec0_stdexec_include "${BVN_EXEC0_STDEXEC_ROOT}/include")
else()
    include(FetchContent)
    # 与 bug/gcc/stdexec-module-ice 记录的是同一个提交；只取源码，不跑 stdexec 自己的 CMake。
    FetchContent_Declare(exec0_stdexec_source
        GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
        GIT_TAG 633c87370b986735b0c8667993b5fc18bf1b3129
        SOURCE_SUBDIR cmake-is-not-used
    )
    FetchContent_MakeAvailable(exec0_stdexec_source)
    set(_exec0_stdexec_include "${exec0_stdexec_source_SOURCE_DIR}/include")
endif()

add_library(exec0_stdexec INTERFACE)
target_include_directories(exec0_stdexec SYSTEM INTERFACE "${_exec0_stdexec_include}")
target_link_libraries(exec0_stdexec INTERFACE Threads::Threads)
message(STATUS "exec-0: stdexec include = ${_exec0_stdexec_include}")
