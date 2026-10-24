# static-dag（header-only，仓库内 demo/static-dag/include）-> 目标 exec0_sdag
# 只在 execution/execution.cppm 的全局模块片段包含 <sdag/sdag.h>；它依赖 stdexec，所以链接 exec0_stdexec。
# clang 上 SDAG_REFLECTION 自动为 0（模板实现），不需要 -freflection。
add_library(exec0_sdag INTERFACE)
target_include_directories(exec0_sdag SYSTEM INTERFACE "${BVN_EXEC0_REPO_ROOT}/demo/static-dag/include")
target_link_libraries(exec0_sdag INTERFACE exec0_stdexec)
message(STATUS "exec-0: sdag include = ${BVN_EXEC0_REPO_ROOT}/demo/static-dag/include")
