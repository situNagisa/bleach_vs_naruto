# nagisa（header-only）-> 目标 exec0_nagisa
# 定位：$NAGISA_ROOT，或 -DBVN_EXEC0_NAGISA_ROOT=...。nagisa 的 concurrency 依赖 stdexec。
include("${CMAKE_CURRENT_LIST_DIR}/locate.cmake")

bvn_exec0_locate(BVN_EXEC0_NAGISA_ROOT
    ENV NAGISA_ROOT
    PROBE include/nagisa/concurrency/lease.h
    DOC "nagisa 源码根目录（含 include/nagisa/concurrency/lease.h）"
)

if(NOT BVN_EXEC0_NAGISA_ROOT_FOUND)
    message(FATAL_ERROR
        "找不到 nagisa：请设置环境变量 NAGISA_ROOT 或 -DBVN_EXEC0_NAGISA_ROOT=<nagisa 根目录>"
        "（需要 include/nagisa/concurrency/lease.h）。当前值：'${BVN_EXEC0_NAGISA_ROOT}'")
endif()

add_library(exec0_nagisa INTERFACE)
target_include_directories(exec0_nagisa SYSTEM INTERFACE "${BVN_EXEC0_NAGISA_ROOT}/include")
target_link_libraries(exec0_nagisa INTERFACE exec0_stdexec)
message(STATUS "exec-0: nagisa include = ${BVN_EXEC0_NAGISA_ROOT}/include")
