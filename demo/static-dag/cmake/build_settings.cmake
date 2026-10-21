# 公共编译设置目标 sdag_build_settings：警告级别、标准库选择、模板深度。
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
    else()
        # clang++ + libc++
        target_compile_options(${TARGET} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -stdlib=libc++
        )
        target_link_options(${TARGET} INTERFACE -stdlib=libc++)
    endif()
endfunction()
