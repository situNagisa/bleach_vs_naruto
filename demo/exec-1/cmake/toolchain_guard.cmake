# 工具链守卫：exec-0 只锚定 Clang（Linux 上 clang++ + libc++，Windows 上 clang-cl + MSVC STL），
# 且必须用 Ninja 生成器。GCC 与 MSVC 的模块 ICE 记录见仓库 bug/gcc、bug/msvc。
function(bvn_exec0_check_toolchain)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR
            "exec-0 只支持 Clang / clang-cl（当前：${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}）。\n"
            "GCC 与 MSVC 在模块 + stdexec 上的 ICE 记录见 bug/gcc 与 bug/msvc。")
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 22.0)
        message(FATAL_ERROR
            "exec-0 锚定最新的 Clang（>= 22），当前：${CMAKE_CXX_COMPILER_VERSION}。请更新 LLVM。")
    endif()
    if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
        message(FATAL_ERROR
            "exec-0 需要 Ninja / Ninja Multi-Config 生成器（当前：${CMAKE_GENERATOR}）。\n"
            "clang-cl 的模块扫描不支持 Visual Studio 生成器（-T ClangCL）。")
    endif()
endfunction()
