# 工具链守卫：sdag 的编译期计算全部用 C++26 静态反射（P2996），目前只有 GCC >= 16（-freflection）实现了它；
# 生成器用 Ninja（与仓库其他 demo 一致）。
function(sdag_check_toolchain)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR
            "sdag 需要支持 C++26 静态反射的编译器，目前只有 GCC >= 16（当前：${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}）。")
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.0)
        message(FATAL_ERROR
            "sdag 需要 GCC >= 16（-freflection），当前：${CMAKE_CXX_COMPILER_VERSION}。")
    endif()
    if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
        message(FATAL_ERROR
            "sdag 需要 Ninja / Ninja Multi-Config 生成器（当前：${CMAKE_GENERATOR}）。")
    endif()
endfunction()
