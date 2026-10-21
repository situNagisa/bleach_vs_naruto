# 工具链守卫：sdag 只锚定 Clang（Linux 上 clang++ + libc++，Windows 上 clang-cl + MSVC STL），
# 且必须用 Ninja 生成器（与仓库其他 demo 一致）。
function(sdag_check_toolchain)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR
            "sdag 只支持 Clang / clang-cl（当前：${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}）。")
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 22.0)
        message(FATAL_ERROR
            "sdag 锚定最新的 Clang（>= 22），当前：${CMAKE_CXX_COMPILER_VERSION}。请更新 LLVM。")
    endif()
    if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
        message(FATAL_ERROR
            "sdag 需要 Ninja / Ninja Multi-Config 生成器（当前：${CMAKE_GENERATOR}）。")
    endif()
endfunction()
