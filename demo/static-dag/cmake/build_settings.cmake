# 公共编译设置目标 sdag_build_settings，以及反射开关。
#
# SDAG_REFLECTION（ON / OFF）：编译期计算用 C++26 静态反射。目前只有 GCC >= 16 的 -freflection 实现了它，
# 所以默认只在 GCC 上打开；Clang 上打开会直接报错。关掉时走 include/sdag/compute/templates/ 的模板实现。
# 头文件会按编译器是否真的打开了反射自动选择（include/sdag/config.h），这里只决定加不加 -freflection。
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_sdag_reflection_default ON)
else()
    set(_sdag_reflection_default OFF)
endif()
option(SDAG_REFLECTION "编译期计算用 C++26 静态反射（需要 GCC >= 16）" ${_sdag_reflection_default})

if(SDAG_REFLECTION AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "SDAG_REFLECTION=ON 需要支持 C++26 静态反射的编译器（目前只有 GCC >= 16）。")
endif()

# 反射要作用于所有包含 sdag 头文件的目标（库的使用者也一样），所以挂在 sdag 目标上，由调用方 INTERFACE 传播。
function(sdag_add_reflection_flags TARGET)
    if(SDAG_REFLECTION)
        target_compile_options(${TARGET} INTERFACE -freflection)
        message(STATUS "sdag: 编译期计算 = 静态反射（-freflection）")
    else()
        message(STATUS "sdag: 编译期计算 = 模板元编程")
    endif()
endfunction()

function(sdag_add_build_settings TARGET)
    add_library(${TARGET} INTERFACE)
    target_compile_features(${TARGET} INTERFACE cxx_std_26)

    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # clang-cl：/EHsc 必须显式给（clang-cl 默认关异常）。
        target_compile_options(${TARGET} INTERFACE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
            /EHsc
            /bigobj
            /wd4100
        )
        target_link_options(${TARGET} INTERFACE /DEBUG)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # clang++ + libc++
        target_compile_options(${TARGET} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -stdlib=libc++
        )
        target_link_options(${TARGET} INTERFACE -stdlib=libc++)
    else()
        # g++ + libstdc++
        target_compile_options(${TARGET} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endfunction()
